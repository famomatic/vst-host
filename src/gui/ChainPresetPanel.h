#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "graph/GraphEngine.h"
#include "persist/ChainPreset.h"

#include <functional>
#include <memory>
#include <vector>

namespace host::gui
{
/// Macro panel for chain presets: capture the current effect-chain sound as a
/// preset, and switch between stored presets with a single click. Switching
/// pushes every captured parameter through requestParameterChange so the
/// audio thread applies them together at the next block boundary - delay-free.
class ChainPresetPanel : public juce::Component,
                          private juce::ListBoxModel
{
public:
    ChainPresetPanel(std::shared_ptr<host::graph::GraphEngine> graphEngine,
                     juce::File presetDirectory,
                     std::function<void()> onStateChanged = {});

    void refresh();
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existing) override;

    void scanPresets();
    void captureCurrentChain();
    void recallPreset(int index);

    std::shared_ptr<host::graph::GraphEngine> graph;
    juce::File presetDirectory;
    std::function<void()> onStateChangedCallback;

    std::vector<juce::File> presetFiles;
    std::vector<juce::String> presetNames;
    int selectedRow { -1 };

    juce::ListBox presetList { "ChainPresets", this };
    juce::TextButton addButton { "+" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton deleteButton { "Delete" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainPresetPanel)
};
} // namespace host::gui