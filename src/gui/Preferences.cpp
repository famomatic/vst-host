#include "gui/Preferences.h"

namespace host::gui
{
    PreferencesComponent::PreferencesComponent(host::audio::DeviceEngine& engine,
                                               std::shared_ptr<host::plugin::PluginScanner> scanner)
        : deviceEngine(engine), pluginScanner(std::move(scanner))
    {
        addAndMakeVisible(tabs);
        buildAudioTab();
        buildPluginTab();
        pluginPathList.setRowHeight(24);
    }

    void PreferencesComponent::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::darkgrey);
    }

    void PreferencesComponent::resized()
    {
        tabs.setBounds(getLocalBounds().reduced(10));
        layoutAudioTab();
        layoutPluginTab();
    }

    void PreferencesComponent::buildAudioTab()
    {
        audioTab = new juce::Component();
        audioTab->addAndMakeVisible(driverBox);
        audioTab->addAndMakeVisible(deviceBox);
        audioTab->addAndMakeVisible(engineSampleRateBox);
        audioTab->addAndMakeVisible(engineBlockBox);

        tabs.addTab("Audio", juce::Colours::grey, audioTab, true);

        driverBox.addItem("ASIO", 1);
        driverBox.addItem("WASAPI", 2);
        driverBox.setSelectedId(1);

        engineSampleRateBox.addItem("44100", 1);
        engineSampleRateBox.addItem("48000", 2);
        engineSampleRateBox.addItem("96000", 3);
        engineSampleRateBox.setSelectedId(2);
        engineSampleRateBox.onChange = [this]
        {
            auto cfg = deviceEngine.getConfig();
            cfg.sampleRate = engineSampleRateBox.getText().getDoubleValue();
            deviceEngine.setConfig(cfg);
        };

        engineBlockBox.addItem("128", 1);
        engineBlockBox.addItem("256", 2);
        engineBlockBox.addItem("512", 3);
        engineBlockBox.setSelectedId(2);
        engineBlockBox.onChange = [this]
        {
            auto cfg = deviceEngine.getConfig();
            cfg.blockSize = engineBlockBox.getText().getIntValue();
            deviceEngine.setConfig(cfg);
        };

        refreshDeviceList();
    }

    void PreferencesComponent::buildPluginTab()
    {
        pluginTab = new juce::Component();
        pluginTab->addAndMakeVisible(pluginPathList);
        pluginTab->addAndMakeVisible(addPathButton);
        pluginTab->addAndMakeVisible(removePathButton);
        pluginTab->addAndMakeVisible(rescanButton);

        tabs.addTab("Plugins", juce::Colours::grey, pluginTab, true);

        if (pluginScanner)
        {
            pluginPaths.clear();
            for (auto& path : pluginScanner->getSearchPaths())
                pluginPaths.push_back(path);
        }

        pluginPathList.updateContent();

        addPathButton.onClick = [this]
        {
            juce::FileChooser chooser("Select plugin directory", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
            if (chooser.browseForDirectory())
            {
                auto dir = chooser.getResult();
                pluginPaths.push_back(dir);
                if (pluginScanner)
                    pluginScanner->addSearchPath(dir);
                pluginPathList.updateContent();
                pluginPathList.repaint();
            }
        };

        removePathButton.onClick = [this]
        {
            auto selected = pluginPathList.getSelectedRow();
            if (selected >= 0 && selected < static_cast<int>(pluginPaths.size()))
            {
                auto file = pluginPaths[static_cast<size_t>(selected)];
                pluginPaths.erase(pluginPaths.begin() + selected);
                if (pluginScanner)
                    pluginScanner->removeSearchPath(file);
                pluginPathList.updateContent();
                pluginPathList.repaint();
            }
        };

        rescanButton.onClick = [this]
        {
            if (pluginScanner)
                pluginScanner->scanAsync();
        };
    }

    void PreferencesComponent::layoutAudioTab()
    {
        if (audioTab == nullptr)
            return;

        auto area = audioTab->getLocalBounds().reduced(20);
        auto lineHeight = 32;
        driverBox.setBounds(area.removeFromTop(lineHeight));
        area.removeFromTop(10);
        deviceBox.setBounds(area.removeFromTop(lineHeight));
        area.removeFromTop(10);
        engineSampleRateBox.setBounds(area.removeFromTop(lineHeight));
        area.removeFromTop(10);
        engineBlockBox.setBounds(area.removeFromTop(lineHeight));
    }

    void PreferencesComponent::layoutPluginTab()
    {
        if (pluginTab == nullptr)
            return;

        auto area = pluginTab->getLocalBounds().reduced(10);
        auto controls = area.removeFromBottom(32);
        addPathButton.setBounds(controls.removeFromLeft(100).reduced(2));
        removePathButton.setBounds(controls.removeFromLeft(100).reduced(2));
        rescanButton.setBounds(controls.removeFromLeft(100).reduced(2));
        pluginPathList.setBounds(area);
    }

    void PreferencesComponent::refreshDeviceList()
    {
        deviceBox.clear();
        deviceBox.addItem("Default Device", 1);
        deviceBox.setSelectedId(1);
    }

    int PreferencesComponent::getNumRows()
    {
        return static_cast<int>(pluginPaths.size());
    }

    void PreferencesComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
    {
        if (row < 0 || row >= getNumRows())
            return;

        auto bounds = juce::Rectangle<int>(0, 0, width, height);
        g.setColour(rowIsSelected ? juce::Colours::darkorange : juce::Colours::transparentBlack);
        g.fillRect(bounds);
        g.setColour(juce::Colours::white);
        g.drawText(pluginPaths[static_cast<size_t>(row)].getFullPathName(), bounds.reduced(4), juce::Justification::centredLeft);
    }
}
