#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

#include "graph/GraphEngine.h"

namespace host::gui
{
    class PluginSettingsComponent : public juce::Component,
                                    private juce::Timer
    {
    public:
        PluginSettingsComponent(std::shared_ptr<host::graph::GraphEngine> graphEngine,
                                host::graph::GraphEngine::NodeId nodeId,
                                std::function<void()> onSettingsChanged);
        ~PluginSettingsComponent() override = default;

        void refresh();
        void resized() override;

    private:
        void updateContent();
        void commitNameChange();
        void applyBypassState();
        void openEditor();
        void savePreset();
        void loadPreset();
        void timerCallback() override;
        void closeHostDialogIfPresent();

        std::weak_ptr<host::graph::GraphEngine> graph;
        host::graph::GraphEngine::NodeId targetId;
        std::function<void()> onSettingsChanged;

        juce::Label nameLabel;
        juce::TextEditor nameEditor;
        juce::Label statusLabel;
        juce::Label statusValue;
        juce::Label formatLabel;
        juce::Label formatValue;
        juce::Label pathLabel;
        juce::Label pathValue;
        juce::Label inputsLabel;
        juce::Label inputsValue;
        juce::Label outputsLabel;
        juce::Label outputsValue;
        juce::Label latencyLabel;
        juce::Label latencyValue;
        juce::ToggleButton bypassToggle;
        juce::TextButton openEditorButton;
        juce::TextButton savePresetButton;
        juce::TextButton loadPresetButton;
        bool closedDialog_ { false };
    };
} // namespace host::gui
