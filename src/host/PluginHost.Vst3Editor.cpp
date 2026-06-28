#include "host/PluginHostVst3Editor.h"

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

namespace host::plugin::vst3editor
{
namespace
{
class Vst3EditorComponent final : public juce::Component,
                                  private juce::Timer,
                                  public Steinberg::IPlugFrame
{
    public:
    Vst3EditorComponent(const std::function<Steinberg::IPlugView*()>& createViewIn,
                        const std::function<void(Steinberg::IPlugView*)>& releaseViewIn)
            : createView(createViewIn),
              releaseView(releaseViewIn)
        {
            setOpaque(false);
            ensureView();
            adjustToInitialSize();
        }

    ~Vst3EditorComponent() override
    {
        detachView();
        releaseCurrentView();
    }

    [[nodiscard]] bool isViewAvailable() const noexcept { return view != nullptr; }

    void parentHierarchyChanged() override
    {
        juce::Component::parentHierarchyChanged();
        attachIfPossible();
        updateScaleFactor();
    }

    void visibilityChanged() override
    {
        juce::Component::visibilityChanged();
        if (isShowing())
            attachIfPossible();
        else
            detachView();
    }

    void focusGained(juce::Component::FocusChangeType cause) override
    {
        juce::Component::focusGained(cause);
        if (view)
            view->onFocus(true);
    }

    void focusLost(juce::Component::FocusChangeType cause) override
    {
        juce::Component::focusLost(cause);
        if (view)
            view->onFocus(false);
    }

    void resized() override
    {
        juce::Component::resized();
        // Keep the plug-in view's logical size in sync with the component even
        // before the native view is attached, otherwise an early setSize() from
        // the host leaves the embedded view at its initial size and the content
        // appears clipped inside a correctly-sized window.
        if (view)
        {
            Steinberg::ViewRect rect { 0, 0, getWidth(), getHeight() };
            view->onSize(&rect);
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

    void timerCallback() override
    {
        // Retries attaching the plug-in view once the native peer is ready.
        if (attached || ! isShowing())
        {
            stopTimer();
            return;
        }
        attachIfPossible();
    }

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* requestedView, Steinberg::ViewRect* newSize) override
    {
        if (requestedView != view || newSize == nullptr)
            return Steinberg::kInvalidArgument;

        const int width = newSize->right - newSize->left;
        const int height = newSize->bottom - newSize->top;

        juce::Component::SafePointer<Vst3EditorComponent> safeComponent(this);
        Steinberg::IPtr<Steinberg::IPlugView> retained(requestedView);
        juce::MessageManager::callAsync([safeComponent, width, height, retained]()
        {
            if (auto* comp = safeComponent.getComponent())
            {
                comp->setSize(width, height);
                Steinberg::ViewRect rect { 0, 0, width, height };
                retained->onSize(&rect);
            }
        });

        return Steinberg::kResultOk;
    }

private:
    void ensureView()
    {
        if (view || ! createView)
            return;

        view = createView();
        if (! view)
            return;

        Steinberg::IPlugViewContentScaleSupport* support = nullptr;
        if (view->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid, reinterpret_cast<void**>(&support)) == Steinberg::kResultOk && support != nullptr)
            scaleSupport = Steinberg::IPtr<Steinberg::IPlugViewContentScaleSupport>::adopt(support);

        adjustToInitialSize();
    }

    void adjustToInitialSize()
    {
        if (! view)
            return;

        Steinberg::ViewRect rect {};
        const bool sizeRetrieved = (view->getSize(&rect) == Steinberg::kResultOk);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        if (! sizeRetrieved || width <= 0 || height <= 0)
        {
            width = 800;
            height = 600;
        }

        setSize(width, height);
        Steinberg::ViewRect finalRect { 0, 0, width, height };
        view->onSize(&finalRect);
    }

    void attachIfPossible()
    {
        if (attached || ! isShowing())
            return;

        ensureView();
        if (! view)
            return;

        // The peer (native window) may not exist yet when the component is
        // first added to a dialog. Poll until it appears, then attach.
        if (getPeer() == nullptr)
        {
            if (! isTimerRunning())
                startTimerHz(30);
            return;
        }

        if (auto* peer = getPeer())
        {
            void* nativeHandle = peer->getNativeHandle();
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

            if (view->attached(nativeHandle, platformType) == Steinberg::kResultOk)
            {
                view->setFrame(this);
                attached = true;
                updateScaleFactor();
                Steinberg::ViewRect rect { 0, 0, getWidth(), getHeight() };
                view->onSize(&rect);
                stopTimer();
            }
        }
    }

    void detachView()
    {
        if (! view || ! attached)
            return;

        stopTimer();
        view->setFrame(nullptr);
        view->removed();
        attached = false;
    }

    void releaseCurrentView()
    {
        scaleSupport = nullptr;
        if (view)
        {
            if (releaseView)
                releaseView(view);
            view = nullptr;
        }
    }

    void updateScaleFactor()
    {
        if (! scaleSupport || ! view)
            return;

        if (auto* peer = getPeer())
        {
            const auto scale = peer->getPlatformScaleFactor();
            scaleSupport->setContentScaleFactor(scale);
        }
    }

    std::function<Steinberg::IPlugView*()> createView;
    std::function<void(Steinberg::IPlugView*)> releaseView;
    Steinberg::IPlugView* view { nullptr };
    Steinberg::IPtr<Steinberg::IPlugViewContentScaleSupport> scaleSupport;
    bool attached { false };
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
