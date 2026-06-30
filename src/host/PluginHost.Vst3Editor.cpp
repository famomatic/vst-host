#include "host/PluginHostVst3Editor.h"

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #include <juce_gui_extra/embedding/juce_HWNDComponent.h>
#endif

namespace host::plugin::vst3editor
{
namespace
{
// Reentrancy guard. The plugin<->host sizing loop is the classic source of
// editor clipping bugs: host setSize() -> resize notification -> plugin
// resizeView() -> host setSize() ... JUCE breaks the cycle with a recursive
// resize flag; we mirror that exactly.
struct ScopedFlag
{
    bool& flag;
    explicit ScopedFlag(bool& f) : flag(f) { flag = true; }
    ~ScopedFlag() { flag = false; }
};

// VST3 editor host component. Mirrors juce::VST3PluginWindow, which is the
// implementation Light Host (and every JUCE-based host) relies on. The key
// detail is that the plugin's IPlugView attaches to a dedicated HWNDComponent
// child window - never the dialog's HWND - and peer/visibility transitions
// are tracked via ComponentMovementWatcher so attach/detach happens at the
// right moment instead of being polled on a timer.
class Vst3EditorComponent final : public juce::Component,
                                 private juce::ComponentMovementWatcher,
                                 public Steinberg::IPlugFrame
{
public:
    Vst3EditorComponent(const std::function<Steinberg::IPlugView*()>& createViewIn,
                        const std::function<void(Steinberg::IPlugView*)>& releaseViewIn)
        : juce::ComponentMovementWatcher(this),
          createView(createViewIn),
          releaseView(releaseViewIn)
    {
        setOpaque(true);
        setSize(10, 10);

        ensureView();

        if (view)
        {
            view->setFrame(this);

            Steinberg::IPlugViewContentScaleSupport* support = nullptr;
            if (view->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid,
                                     reinterpret_cast<void**>(&support)) == Steinberg::kResultOk
                && support != nullptr)
            {
                scaleSupport = Steinberg::IPtr<Steinberg::IPlugViewContentScaleSupport>::adopt(support);
            }

            updateScaleFactor();
            resizeToFit();
        }
    }

    ~Vst3EditorComponent() override
    {
        detachView();
        releaseCurrentView();
    }

    [[nodiscard]] bool isViewAvailable() const noexcept
    {
        return view != nullptr || (createView && releaseView);
    }

    // Pin the desktop scale to 1.0 so the embedded IPlugView is not scaled
    // twice (host desktop scale + the plugin's own content scale factor).
    float getDesktopScaleFactor() const override { return 1.0f; }

    void visibilityChanged() override
    {
        juce::Component::visibilityChanged();
        if (isShowing())
            attachPluginWindow();
        else
            detachView();
        // Re-fit to the view's own size on visibility transitions. Do NOT
        // call componentMovedOrResized here: that pushes the host's current
        // bounds back into onSize(), which during initial show is still the
        // 10x10 constructor size (or a stale default) and shrinks the view a
        // little more on every open/close cycle.
        resizeToFit();
        updateScaleFactor();
    }

    void focusGained(juce::Component::FocusChangeType) override
    {
        if (view) view->onFocus(true);
    }
    void focusLost(juce::Component::FocusChangeType) override
    {
        if (view) view->onFocus(false);
    }

    void resized() override
    {
        juce::Component::resized();
        embeddedComponent.setBounds(getLocalBounds());

        if (! view || recursiveResize)
            return;

        if (view->canResize() == Steinberg::kResultTrue)
        {
            const auto hostBounds = getLocalBounds();
            if (hostBounds.getWidth() <= 0 || hostBounds.getHeight() <= 0)
                return;

            auto scaled = componentToVst3Rect(hostBounds);
            auto constrained = scaled;
            view->checkSizeConstraint(&constrained);

            // The host may freely push a LARGER size to the view (user dragged
            // the dialog bigger). But never push a size SMALLER than the view's
            // own reported size: during the initial show and on peer/visibility
            // transitions the host bounds can still be the 10x10 constructor
            // size or a stale default, and feeding that to onSize() makes the
            // view adopt it - then it reports that smaller rect next time,
            // shrinking the editor a bit more on every open/close cycle.
            const bool wouldShrinkView = lastViewSize.getWidth() > 0
                && (constrained.getWidth() < lastViewSize.getWidth()
                    || constrained.getHeight() < lastViewSize.getHeight());
            if (wouldShrinkView)
                return;

            if (constrained.getWidth() != scaled.getWidth()
                || constrained.getHeight() != scaled.getHeight())
            {
                ScopedFlag g(recursiveResize);
                const auto logical = vst3ToComponentRect(constrained);
                setSize(logical.getWidth(), logical.getHeight());
            }

            // Only confirm a size to the view when the host is genuinely
            // driving it larger (or matching) - otherwise leave the view's own
            // size untouched so resizeToFit()/resizeView() stay in charge.
            if (constrained.getWidth() >= lastViewSize.getWidth()
                && constrained.getHeight() >= lastViewSize.getHeight())
            {
                view->onSize(&constrained);
                lastViewSize = juce::Rectangle<int>(constrained.getWidth(), constrained.getHeight());
            }
        }
        else
        {
            Steinberg::ViewRect rect;
            if (view->getSize(&rect) == Steinberg::kResultOk)
                embeddedComponent.setSize(juce::jmax(10, rect.right), juce::jmax(10, rect.bottom));
        }
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
        if (obj == nullptr)
            return Steinberg::kInvalidArgument;

        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)
            || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid))
        {
            *obj = static_cast<Steinberg::IPlugFrame*>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* requestedView, Steinberg::ViewRect* newSize) override
    {
        if (requestedView != view || newSize == nullptr)
            return Steinberg::kInvalidArgument;

        const int width = newSize->right - newSize->left;
        const int height = newSize->bottom - newSize->top;
        if (width <= 0 || height <= 0)
            return Steinberg::kInvalidArgument;

        ScopedFlag g(recursiveResize);

        const auto logical = vst3ToComponentRect(*newSize);
        setSize(logical.getWidth(), logical.getHeight());
        embeddedComponent.setSize(logical.getWidth(), logical.getHeight());
        lastViewSize = juce::Rectangle<int>(logical.getWidth(), logical.getHeight());

        // VST3 workflow requires the host to confirm the new size via onSize().
        // Plugins that wait for this confirmation otherwise stall and render at
        // their previous (often default/tiny) size - symptom 3.
        Steinberg::ViewRect confirmed = componentToVst3Rect(getLocalBounds());
        if (! isInOnSize)
        {
            ScopedFlag og(isInOnSize);
            view->onSize(&confirmed);
        }
        return Steinberg::kResultOk;
    }

private:
    // --- ComponentMovementWatcher hooks ---
    void componentMovedOrResized(bool, bool wasResized) override
    {
        if (recursiveResize || ! wasResized)
            return;
        if (getTopLevelComponent() == nullptr || getTopLevelComponent()->getPeer() == nullptr)
            return;

        if (view && view->canResize() == Steinberg::kResultTrue)
        {
            const auto hostBounds = getLocalBounds();
            if (hostBounds.getWidth() <= 0 || hostBounds.getHeight() <= 0)
                return;
            // Same shrink-guard as resized(): only push the host's size to the
            // view when it is at least as large as what the view last reported.
            // Otherwise peer/move transitions (which fire with stale small
            // bounds before the first layout) would shrink the view each time.
            if (lastViewSize.getWidth() > 0
                && (hostBounds.getWidth() < lastViewSize.getWidth()
                    || hostBounds.getHeight() < lastViewSize.getHeight()))
                return;

            ScopedFlag g(recursiveResize);
            auto scaled = componentToVst3Rect(hostBounds);
            view->onSize(&scaled);
            lastViewSize = juce::Rectangle<int>(scaled.getWidth(), scaled.getHeight());
        }
    }
    using juce::ComponentMovementWatcher::componentMovedOrResized;

    void componentVisibilityChanged() override
    {
        attachPluginWindow();
        resizeToFit();
        // Do not call componentMovedOrResized here: it would push the host's
        // (possibly still tiny/stale) bounds into onSize() and shrink the view.
        // resizeToFit() already snaps the component to the view's reported size.
    }
    using juce::ComponentMovementWatcher::componentVisibilityChanged;

    void componentPeerChanged() override {}

    Steinberg::ViewRect componentToVst3Rect(juce::Rectangle<int> r) const
    {
        return { 0, 0, r.getWidth(), r.getHeight() };
    }
    juce::Rectangle<int> vst3ToComponentRect(const Steinberg::ViewRect& vr) const
    {
        return { vr.right, vr.bottom };
    }

    void ensureView()
    {
        if (view || ! createView)
            return;
        view = createView();
    }

    void resizeToFit()
    {
        if (! view)
            return;
        Steinberg::ViewRect rect {};
        if (view->getSize(&rect) != Steinberg::kResultOk)
            return;
        const auto logical = vst3ToComponentRect(rect);
        const int w = juce::jmax(10, logical.getWidth());
        const int h = juce::jmax(10, logical.getHeight());
        lastViewSize = juce::Rectangle<int>(w, h);
        setSize(w, h);
    }

    void attachPluginWindow()
    {
        if (attached || ! view || ! isShowing() || getPeer() == nullptr)
            return;

        void* nativeHandle = embeddedComponent.getHWND();
        if (nativeHandle == nullptr)
            return;

        const char* platformType = nullptr;
#if JUCE_WINDOWS
        platformType = Steinberg::kPlatformTypeHWND;
#elif JUCE_MAC
        platformType = Steinberg::kPlatformTypeNSView;
#else
        platformType = Steinberg::kPlatformTypeX11EmbedWindowID;
#endif

        if (view->isPlatformTypeSupported(platformType) != Steinberg::kResultTrue)
            return;

        embeddedComponent.setBounds(getLocalBounds());

        // Add the embedded HWND host to the hierarchy now that the peer is
        // ready. Adding it in the constructor (before the component has a
        // peer) leaves the embedded native window unparented on some plugins.
        addAndMakeVisible(embeddedComponent);

        if (view->attached(nativeHandle, platformType) == Steinberg::kResultOk)
        {
            attached = true;
            updateScaleFactor();
            // Sync native HWND bounds immediately; some plugins paint at their
            // default size before the first propagated resize otherwise.
            embeddedComponent.updateHWNDBounds();
            resizeToFit();
        }
    }

    void detachView()
    {
        if (! view || ! attached)
            return;
        view->setFrame(nullptr);
        view->removed();
        attached = false;
    }

    void releaseCurrentView()
    {
        scaleSupport = nullptr;
        if (view && releaseView)
        {
            releaseView(view);
            view = nullptr;
        }
    }

    void updateScaleFactor()
    {
        if (! scaleSupport || ! view)
            return;
        float scale = 1.0f;
        if (auto* peer = getPeer())
            scale = peer->getPlatformScaleFactor();
        scaleSupport->setContentScaleFactor(
            static_cast<Steinberg::IPlugViewContentScaleSupport::ScaleFactor>(scale));
    }

    std::function<Steinberg::IPlugView*()> createView;
    std::function<void(Steinberg::IPlugView*)> releaseView;
    Steinberg::IPlugView* view { nullptr };
    Steinberg::IPtr<Steinberg::IPlugViewContentScaleSupport> scaleSupport;
    bool attached { false };
    bool recursiveResize { false };
    bool isInOnSize { false };
    // Last size the view reported/accepted (logical component pixels). Used to
    // stop the host from pushing a smaller size back into onSize() during the
    // initial show and peer/visibility transitions, which is what shrank the
    // editor a little more on every open/close cycle.
    juce::Rectangle<int> lastViewSize;

#if JUCE_WINDOWS
    // Dedicated HWND host. The plugin view attaches to THIS window, never to
    // the dialog's HWND. That keeps the plugin clipped to the editor area
    // instead of painting over (or being clipped by) the dialog chrome.
    struct EmbeddedWindow final : public juce::HWNDComponent
    {
        EmbeddedWindow()
        {
            setOpaque(true);
            inner.addToDesktop(0);
            if (auto* peer = inner.getPeer())
                setHWND(peer->getNativeHandle());
        }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
    private:
        struct Inner final : public juce::Component
        {
            Inner() { setOpaque(true); }
            void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
        };
        Inner inner;
    };
    EmbeddedWindow embeddedComponent;
#else
    juce::Component embeddedComponent;
#endif
};
} // namespace

std::unique_ptr<juce::Component> createEditorComponent(
    const std::function<Steinberg::IPlugView*()>& createView,
    const std::function<void(Steinberg::IPlugView*)>& releaseView)
{
    auto editor = std::make_unique<Vst3EditorComponent>(createView, releaseView);
    if (! editor->isViewAvailable())
        return {};
    return editor;
}
} // namespace host::plugin::vst3editor
