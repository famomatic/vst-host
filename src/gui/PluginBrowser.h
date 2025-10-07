#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

#include "host/PluginScanner.h"

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
        void setOnPluginChosen(std::function<void(const host::plugin::PluginInfo&)> callback);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;
        void returnKeyPressed(int row) override;
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;
        void filterPlugins();
        void triggerAddPlugin(int row);

        std::shared_ptr<host::plugin::PluginScanner> pluginScanner;
        juce::TextEditor searchBox;
        juce::ListBox listBox { "Plugins", this };
        std::vector<host::plugin::PluginInfo> filteredPlugins;
        std::function<void(const host::plugin::PluginInfo&)> onPluginChosen;
    };
}
