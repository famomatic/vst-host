#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

#include "audio/DeviceEngine.h"
#include "host/PluginScanner.h"

namespace host::gui
{
    class PreferencesComponent : public juce::Component,
                                 private juce::ListBoxModel
    {
    public:
        PreferencesComponent(host::audio::DeviceEngine& engine,
                             std::shared_ptr<host::plugin::PluginScanner> scanner,
                             juce::AudioDeviceManager& deviceManager);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void parentHierarchyChanged() override;

    private:
        void buildAudioTab();
        void buildPluginTab();
        void refreshDriverList();
        void refreshDeviceLists();
        void refreshEngineOptions();
        void layoutAudioTab();
        void layoutPluginTab();
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

        host::audio::DeviceEngine& deviceEngine;
        std::shared_ptr<host::plugin::PluginScanner> pluginScanner;
        juce::AudioDeviceManager& deviceManager;

        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        juce::Component* audioTab { nullptr };
        juce::Component* pluginTab { nullptr };
        juce::ComboBox driverBox;
        juce::ComboBox inputDeviceBox;
        juce::ComboBox outputDeviceBox;
        juce::ComboBox engineSampleRateBox;
        juce::ComboBox engineBlockBox;
        juce::Label driverLabel;
        juce::Label inputDeviceLabel;
        juce::Label outputDeviceLabel;
        juce::Label sampleRateLabel;
        juce::Label blockSizeLabel;
        juce::ListBox pluginPathList { "PluginPaths", this };
        juce::TextButton addPathButton { "Add" };
        juce::TextButton removePathButton { "Remove" };
        juce::TextButton rescanButton { "Rescan" };
        std::vector<juce::File> pluginPaths;
        bool isUpdating { false };
    };
}
