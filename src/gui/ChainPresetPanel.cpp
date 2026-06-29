#include "gui/ChainPresetPanel.h"

#include "util/Localization.h"

#include <algorithm>

namespace host::gui
{
    using host::i18n::tr;

    ChainPresetPanel::ChainPresetPanel(std::shared_ptr<host::graph::GraphEngine> graphEngine,
                                       juce::File presetDirectoryIn,
                                       std::function<void()> onStateChanged)
        : graph(std::move(graphEngine)),
          presetDirectory(std::move(presetDirectoryIn)),
          onStateChangedCallback(std::move(onStateChanged))
    {
        if (! presetDirectory.isDirectory())
            presetDirectory.createDirectory();

        addAndMakeVisible(presetList);
        addAndMakeVisible(addButton);
        addAndMakeVisible(saveButton);
        addAndMakeVisible(deleteButton);

        addButton.onClick = [this] { captureCurrentChain(); };
        saveButton.onClick = [this]
        {
            if (selectedRow >= 0 && selectedRow < static_cast<int>(presetFiles.size()))
                captureCurrentChain();
        };
        deleteButton.onClick = [this]
        {
            if (selectedRow < 0 || selectedRow >= static_cast<int>(presetFiles.size()))
                return;
            presetFiles[static_cast<size_t>(selectedRow)].deleteFile();
            scanPresets();
            presetList.updateContent();
            presetList.repaint();
        };

        scanPresets();
        presetList.setRowHeight(28);
        presetList.updateContent();
    }

    void ChainPresetPanel::refresh()
    {
        scanPresets();
        presetList.updateContent();
        presetList.repaint();
    }

    void ChainPresetPanel::scanPresets()
    {
        presetFiles.clear();
        presetNames.clear();

        if (! presetDirectory.isDirectory())
            return;

        auto files = presetDirectory.findChildFiles(juce::File::findFiles, false, "*.chainpreset");
        std::sort(files.begin(), files.end(),
                  [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

        for (const auto& file : files)
        {
            host::persist::ChainPreset preset;
            if (preset.load(file))
            {
                presetFiles.push_back(file);
                presetNames.push_back(preset.getName());
            }
        }
    }

    void ChainPresetPanel::captureCurrentChain()
    {
        if (! graph)
            return;

        host::persist::ChainPreset preset;
        preset.captureFromGraph(*graph);

        juce::FileChooser chooser(tr("chainPreset.saveTitle"),
                                  presetDirectory,
                                  "*.chainpreset");
        if (! chooser.browseForFileToSave(true))
            return;

        auto file = chooser.getResult();
        if (file.getFileExtension() != ".chainpreset")
            file = file.withFileExtension(".chainpreset");

        // Default name to the filename so the list shows something sensible.
        if (preset.getName().isEmpty() || preset.getName() == "Chain Preset")
            preset.setName(file.getFileNameWithoutExtension());

        if (! preset.save(file))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("chainPreset.errorTitle"),
                                                   tr("chainPreset.saveFailed"));
            return;
        }

        scanPresets();
        presetList.updateContent();
        presetList.repaint();
    }

    void ChainPresetPanel::recallPreset(int index)
    {
        if (! graph || index < 0 || index >= static_cast<int>(presetFiles.size()))
            return;

        host::persist::ChainPreset preset;
        if (! preset.load(presetFiles[static_cast<size_t>(index)]))
            return;

        // This is the delay-free switch: every captured parameter is pushed
        // through requestParameterChange and lands at the next block boundary.
        preset.applyToGraph(*graph);
    }

    void ChainPresetPanel::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::darkgrey.darker(0.2f));
    }

    void ChainPresetPanel::resized()
    {
        auto area = getLocalBounds().reduced(6);

        auto controls = area.removeFromBottom(32);
        addButton.setBounds(controls.removeFromLeft(60).reduced(2));
        saveButton.setBounds(controls.removeFromLeft(80).reduced(2));
        deleteButton.setBounds(controls.removeFromLeft(80).reduced(2));

        presetList.setBounds(area);
    }

    int ChainPresetPanel::getNumRows()
    {
        return static_cast<int>(presetNames.size());
    }

    void ChainPresetPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
    {
        if (row < 0 || row >= static_cast<int>(presetNames.size()))
            return;

        auto bounds = juce::Rectangle<int>(0, 0, width, height);
        g.setColour(rowIsSelected ? juce::Colours::darkorange : juce::Colours::transparentBlack);
        g.fillRect(bounds);
        g.setColour(juce::Colours::white);
        g.drawText(presetNames[static_cast<size_t>(row)], bounds.reduced(4), juce::Justification::centredLeft);
    }

    void ChainPresetPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
    {
        selectedRow = row;
    }

    void ChainPresetPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
    {
        // Double-click recalls immediately - the "macro" gesture.
        recallPreset(row);
    }

    juce::Component* ChainPresetPanel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existing)
    {
        juce::ignoreUnused(rowNumber, isRowSelected);
        return existing;
    }
} // namespace host::gui