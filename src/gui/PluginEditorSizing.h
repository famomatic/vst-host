#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

#include "graph/GraphEngine.h"

namespace host::gui
{
    // Shared plug-in editor window used by both MainWindow (double-click to
    // open a plug-in editor) and PluginSettingsComponent (openEditor button).
    //
    // This mirrors the reference host (LightHost PluginWindow): a DocumentWindow
    // whose content is the editor component, set with
    // resizeToFitWhenContentChanges = true. The hosted editor component is the
    // single source of truth for its own size; JUCE grows/shrinks the window to
    // follow it. The host never pushes a size back into the editor, so there is
    // no host<->plugin resize loop. That loop was the root cause of three
    // symptoms:
    //   - editors shrinking / content vanishing when the window lost focus
    //   - LUveler/ReverbSolo opening clipped (their async effEditOpen resize was
    //     being clamped back to the size seen at open time)
    //   - LUveler opening at full-screen size (the seed was ignored and the
    //     plugin's oversized default leaked through)
    //
    // For resizable editors a corner resizer is added so the user can grow the
    // window; the editor component receives the new bounds via its resized().
    // For non-resizable editors the window simply tracks the editor.
    //
    // Lifecycle safety: a VST plugin can be removed from the graph (and thus
    // its AudioProcessor destroyed) while the editor window is still open.
    // If the window kept using the editor component after that, it would touch
    // freed memory and crash. The graph-tracked constructor below watches the
    // owning GraphEngine + NodeId via a timer; if the node is gone the window
    // closes itself before anyone can touch the dead editor. This fixes the
    // "open settings -> delete node -> close window -> crash" sequence.

    constexpr int kPluginEditorMinWidth = 50;
    constexpr int kPluginEditorMinHeight = 50;

    class PluginEditorWindow final : public juce::DocumentWindow,
                                     private juce::Timer
    {
    public:
        using NodeId = host::graph::GraphEngine::NodeId;

        // Legacy constructor: caller owns lifetime. The editor is assumed to
        // outlive the window. Prefer the graph-tracked overload below so the
        // window auto-closes when its node is removed from the graph.
        PluginEditorWindow(const juce::String& name,
                           std::unique_ptr<juce::Component> editorComponent,
                           bool editorResizable)
            : juce::DocumentWindow(name,
                                   juce::Colours::darkgrey.darker(0.6f),
                                   juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar(true);

            // resizeToFitWhenContentChanges = true: JUCE keeps the window
            // matched to the content's size, including the asynchronous resizes
            // the plugin performs after the editor is opened. This is the
            // crucial flag from the reference host.
            setContentOwned(editorComponent.release(), true);

            // Clamp to a sane minimum so tiny content never collapses the window,
            // and cap to the work area so it never opens taller than the screen.
            const auto userArea = resolveUserArea();
            setResizeLimits(kPluginEditorMinWidth,
                            kPluginEditorMinHeight,
                            userArea.getWidth(),
                            userArea.getHeight());

            setResizable(editorResizable, editorResizable);

            centreAroundSize();
            setVisible(true);
        }

        // Graph-tracked constructor: the window watches the node identified by
        // nodeId in graphEngine. When that node disappears (removed from the
        // graph, or the graph itself is reset/replaced), the window closes
        // itself on the message thread and releases the editor, so the user
        // never interacts with a plugin that no longer exists.
        PluginEditorWindow(const juce::String& name,
                           std::unique_ptr<juce::Component> editorComponent,
                           bool editorResizable,
                           std::weak_ptr<host::graph::GraphEngine> graphEngine,
                           NodeId nodeId)
            : PluginEditorWindow(name, std::move(editorComponent), editorResizable)
        {
            trackedGraph_ = std::move(graphEngine);
            trackedNodeId_ = nodeId;
            isTracked_ = true;
            startTimerHz(20);
        }

        ~PluginEditorWindow() override { clearContentComponent(); }

        void closeButtonPressed() override { delete this; }

    private:
        void timerCallback() override
        {
            if (! isTracked_)
                return;

            bool shouldClose = false;
            if (auto graph = trackedGraph_.lock())
            {
                // getNode returns null when the node id is no longer registered
                // (removed or graph cleared). That is the signal to close.
                if (graph->getNode(trackedNodeId_) == nullptr)
                    shouldClose = true;
            }
            else
            {
                // The graph itself was destroyed. Close immediately.
                shouldClose = true;
            }

            if (shouldClose)
            {
                stopTimer();
                isTracked_ = false;
                // Close on the next message round to avoid re-entrancy while
                // the audio thread may still be mid-teardown. Use a
                // SafePointer so that if the user already closed the window in
                // the meantime we do not touch a deleted object.
                juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginEditorWindow>(this)]()
                {
                    if (auto* w = safe.getComponent())
                        w->closeButtonPressed();
                });
            }
        }

        static juce::Rectangle<int> resolveUserArea()
        {
            const auto* display = juce::Desktop::getInstance().getDisplays()
                .getDisplayForPoint(juce::Point<int>(200, 200));
            return display != nullptr ? display->userArea
                                       : juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        }

        void centreAroundSize()
        {
            const auto userArea = resolveUserArea();

            // Keep the window on screen and modestly inset from the edges,
            // regardless of how large the content reported itself. Some plugins
            // (LUveler) report a full-screen-sized rect on open; without this
            // the window would cover the whole desktop.
            auto bounds = getBounds();
            const int inset = 40;
            if (bounds.getWidth() > userArea.getWidth() - inset)
                bounds.setWidth(userArea.getWidth() - inset);
            if (bounds.getHeight() > userArea.getHeight() - inset)
                bounds.setHeight(userArea.getHeight() - inset);
            bounds = bounds.constrainedWithin(userArea.reduced(inset));
            setBounds(bounds);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)

        bool isTracked_ { false };
        std::weak_ptr<host::graph::GraphEngine> trackedGraph_;
        NodeId trackedNodeId_ {};
    };
} // namespace host::gui
