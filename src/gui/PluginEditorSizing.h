#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <limits>
#include <memory>

namespace host::gui
{
    // Shared editor-sizing helpers used by both MainWindow (double-click to
    // open a plug-in editor) and PluginSettingsComponent (openEditor button).
    // Centralizing this keeps the two entry points consistent: editors open at
    // their preferred size, are clamped to the screen, and refit when the
    // hosted editor component reports an asynchronous size change.

    constexpr int kPluginEditorMinWidth = 50;
    constexpr int kPluginEditorMinHeight = 50;
    constexpr int kPluginEditorMaxWidth = 4096;
    constexpr int kPluginEditorMaxHeight = 3072;

    inline const juce::Identifier kPluginEditorControllerProperty { "pluginEditorController" };

    struct PluginEditorSizing
    {
        int minContentWidth {};
        int minContentHeight {};
        int maxContentWidth {};
        int maxContentHeight {};
        int targetContentWidth {};
        int targetContentHeight {};
    };

    [[nodiscard]] inline PluginEditorSizing calculatePluginEditorSizing(juce::Component& editorComponent, bool editorResizable)
    {
        int preferredContentWidth = juce::jmax(1, editorComponent.getWidth());
        int preferredContentHeight = juce::jmax(1, editorComponent.getHeight());

        if (preferredContentWidth <= 1)
            preferredContentWidth = kPluginEditorMinWidth;

        if (preferredContentHeight <= 1)
            preferredContentHeight = kPluginEditorMinHeight;

        // For non-resizable editors the window is fixed to the editor's
        // preferred size. For resizable ones, clamp the user-resize range to
        // the editor minimum/maximum so the content never gets clipped.
        int minContentWidth = editorResizable ? kPluginEditorMinWidth : preferredContentWidth;
        int minContentHeight = editorResizable ? kPluginEditorMinHeight : preferredContentHeight;
        int maxContentWidth = editorResizable ? kPluginEditorMaxWidth : preferredContentWidth;
        int maxContentHeight = editorResizable ? kPluginEditorMaxHeight : preferredContentHeight;

        if (auto* audioProcessorEditor = dynamic_cast<juce::AudioProcessorEditor*>(&editorComponent))
        {
            if (auto* constrainer = audioProcessorEditor->getConstrainer())
            {
                const int constrainerMinWidth = constrainer->getMinimumWidth();
                const int constrainerMinHeight = constrainer->getMinimumHeight();

                if (constrainerMinWidth > 0)
                    minContentWidth = juce::jmax(minContentWidth, constrainerMinWidth);

                if (constrainerMinHeight > 0)
                    minContentHeight = juce::jmax(minContentHeight, constrainerMinHeight);

                const int constrainerMaxWidth = constrainer->getMaximumWidth();
                if (constrainerMaxWidth > 0)
                    maxContentWidth = juce::jmin(maxContentWidth, constrainerMaxWidth);

                const int constrainerMaxHeight = constrainer->getMaximumHeight();
                if (constrainerMaxHeight > 0)
                    maxContentHeight = juce::jmin(maxContentHeight, constrainerMaxHeight);
            }
        }

        // Keep the preferred size inside the resolved limits so the dialog
        // opens at the editor's requested size instead of collapsing to the
        // minimum, which previously caused the 'opens at minimum size' bug.
        preferredContentWidth = juce::jlimit(minContentWidth, maxContentWidth, preferredContentWidth);
        preferredContentHeight = juce::jlimit(minContentHeight, maxContentHeight, preferredContentHeight);

        const int resolvedMaxContentWidth = juce::jmax(minContentWidth, maxContentWidth);
        const int resolvedMaxContentHeight = juce::jmax(minContentHeight, maxContentHeight);

        const int targetContentWidth = juce::jlimit(minContentWidth, resolvedMaxContentWidth, preferredContentWidth);
        const int targetContentHeight = juce::jlimit(minContentHeight, resolvedMaxContentHeight, preferredContentHeight);

        return { minContentWidth, minContentHeight, resolvedMaxContentWidth, resolvedMaxContentHeight, targetContentWidth, targetContentHeight };
    }

    inline void applyPluginEditorSizingToDialog(juce::DialogWindow& dialog, juce::Component& editorComponent, const PluginEditorSizing& sizing)
    {
        const auto contentBorder = dialog.getContentComponentBorder();
        const auto windowBorder = dialog.getBorderThickness();
        const int horizontalPadding = contentBorder.getLeftAndRight() + windowBorder.getLeftAndRight();
        const int verticalPadding = contentBorder.getTopAndBottom() + windowBorder.getTopAndBottom();

        const auto currentBounds = dialog.getBounds();
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(currentBounds);
        const auto userArea = display != nullptr ? display->userArea
                                                 : juce::Desktop::getInstance().getDisplays().getTotalBounds(true);

        const int availableContentWidth = juce::jmax(1, userArea.getWidth() - horizontalPadding);
        const int availableContentHeight = juce::jmax(1, userArea.getHeight() - verticalPadding);

        const int screenMinContentWidth = juce::jmin(sizing.minContentWidth, availableContentWidth);
        const int screenMinContentHeight = juce::jmin(sizing.minContentHeight, availableContentHeight);

        int screenMaxContentWidth = sizing.maxContentWidth == std::numeric_limits<int>::max()
                                        ? availableContentWidth
                                        : juce::jmin(sizing.maxContentWidth, availableContentWidth);
        int screenMaxContentHeight = sizing.maxContentHeight == std::numeric_limits<int>::max()
                                          ? availableContentHeight
                                          : juce::jmin(sizing.maxContentHeight, availableContentHeight);

        screenMaxContentWidth = juce::jmax(screenMinContentWidth, screenMaxContentWidth);
        screenMaxContentHeight = juce::jmax(screenMinContentHeight, screenMaxContentHeight);

        const int minWindowWidth = screenMinContentWidth + horizontalPadding;
        const int minWindowHeight = screenMinContentHeight + verticalPadding;
        const int maxWindowWidth = screenMaxContentWidth + horizontalPadding;
        const int maxWindowHeight = screenMaxContentHeight + verticalPadding;

        dialog.setResizeLimits(minWindowWidth, minWindowHeight, maxWindowWidth, maxWindowHeight);

        const int desiredWindowWidth = sizing.targetContentWidth + horizontalPadding;
        const int desiredWindowHeight = sizing.targetContentHeight + verticalPadding;

        const int targetWindowWidth = juce::jlimit(minWindowWidth, maxWindowWidth, desiredWindowWidth);
        const int targetWindowHeight = juce::jlimit(minWindowHeight, maxWindowHeight, desiredWindowHeight);

        auto adjustedBounds = currentBounds.withSizeKeepingCentre(targetWindowWidth, targetWindowHeight);
        adjustedBounds = adjustedBounds.constrainedWithin(userArea);
        dialog.setBounds(adjustedBounds);

        const int targetContentWidth = juce::jlimit(screenMinContentWidth, screenMaxContentWidth, sizing.targetContentWidth);
        const int targetContentHeight = juce::jlimit(screenMinContentHeight, screenMaxContentHeight, sizing.targetContentHeight);

        if (editorComponent.getWidth() != targetContentWidth || editorComponent.getHeight() != targetContentHeight)
            editorComponent.setSize(targetContentWidth, targetContentHeight);
    }

    // Listens for editor-component size changes (plugins report their size
    // asynchronously after attach) and refits the wrapping dialog so the
    // window always matches the editor rather than clipping it or staying
    // oversized at the seeded default size.
    class PluginEditorWindowController : public juce::ComponentListener
    {
    public:
        PluginEditorWindowController(juce::DialogWindow& dialogIn,
                                     juce::Component& editorIn,
                                     bool editorResizableIn)
            : dialog(&dialogIn),
              editor(&editorIn),
              editorResizable(editorResizableIn)
        {
            if (auto* comp = editor.getComponent())
                comp->addComponentListener(this);
        }

       ~PluginEditorWindowController() override
       {
           if (auto* comp = editor.getComponent())
                comp->removeComponentListener(this);
       }

        void applySizing()
        {
            if (auto* dialogPtr = dialog.getComponent())
                if (auto* editorPtr = editor.getComponent())
                    applySizingInternal(*dialogPtr, *editorPtr);
        }

        void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
        {
            if (wasResized)
                applySizing();
        }

    private:
        void applySizingInternal(juce::DialogWindow& dialogWindow, juce::Component& editorComponent)
        {
            if (updating)
                return;

            const juce::ScopedValueSetter<bool> scope(updating, true);
            const auto sizing = calculatePluginEditorSizing(editorComponent, editorResizable);
            applyPluginEditorSizingToDialog(dialogWindow, editorComponent, sizing);
        }

        bool updating { false };
        juce::Component::SafePointer<juce::DialogWindow> dialog;
        juce::Component::SafePointer<juce::Component> editor;
        bool editorResizable { false };
    };

    // Owned by the dialog's property set so its lifetime tracks the dialog.
    struct PluginEditorControllerAttachment final : public juce::DynamicObject
    {
        explicit PluginEditorControllerAttachment(std::unique_ptr<PluginEditorWindowController> controllerIn)
            : controller(std::move(controllerIn))
        {
        }

        std::unique_ptr<PluginEditorWindowController> controller;
    };
} // namespace host::gui
