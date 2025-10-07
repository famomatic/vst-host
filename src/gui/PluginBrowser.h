#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "host/PluginScanner.h"
#include <memory>
#include <vector>

namespace host::gui
{
    class PluginBrowser : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::ChangeListener
    {
    public:
        PluginBrowser();
        ~PluginBrowser() override;

        void setScanner(std::shared_ptr<host::plugin::PluginScanner> scanner);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;
        void filterPlugins();

        std::shared_ptr<host::plugin::PluginScanner> pluginScanner;
        juce::TextEditor searchBox;
        juce::ListBox listBox { "Plugins", this };
        std::vector<host::plugin::PluginInfo> filteredPlugins;
    };
}
