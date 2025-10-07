#include "gui/PluginBrowser.h"

namespace host::gui
{
    PluginBrowser::PluginBrowser()
    {
        addAndMakeVisible(searchBox);
        addAndMakeVisible(listBox);

        listBox.setModel(this);
        searchBox.setTextToShowWhenEmpty("Search plugins", juce::Colours::grey);
        searchBox.onTextChange = [this]
        {
            filterPlugins();
        };
    }

    PluginBrowser::~PluginBrowser()
    {
        if (pluginScanner)
            pluginScanner->removeChangeListener(this);
    }

    void PluginBrowser::setScanner(std::shared_ptr<host::plugin::PluginScanner> scanner)
    {
        if (pluginScanner)
            pluginScanner->removeChangeListener(this);

        pluginScanner = std::move(scanner);

        if (pluginScanner)
            pluginScanner->addChangeListener(this);

        filterPlugins();
    }

    void PluginBrowser::setOnPluginChosen(std::function<void(const host::plugin::PluginInfo&)> callback)
    {
        onPluginChosen = std::move(callback);
    }

    void PluginBrowser::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::darkgrey.darker(0.4f));
    }

    void PluginBrowser::resized()
    {
        auto area = getLocalBounds();
        searchBox.setBounds(area.removeFromTop(28).reduced(4));
        listBox.setBounds(area);
    }

    int PluginBrowser::getNumRows()
    {
        return static_cast<int>(filteredPlugins.size());
    }

    void PluginBrowser::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
    {
        if (row < 0 || row >= getNumRows())
            return;

        auto& info = filteredPlugins[static_cast<size_t>(row)];
        auto bounds = juce::Rectangle<int>(0, 0, width, height);

        g.setColour(rowIsSelected ? juce::Colours::darkorange : juce::Colours::transparentBlack);
        g.fillRect(bounds);

        g.setColour(juce::Colours::white);
        auto label = juce::String(info.name) + " (" + (info.format == host::plugin::PluginFormat::VST3 ? "VST3" : "VST2") + ")";
        g.drawFittedText(label, bounds.reduced(8), juce::Justification::centredLeft, 1);
    }

    void PluginBrowser::listBoxItemClicked(int row, const juce::MouseEvent& event)
    {
        if (event.getNumberOfClicks() > 1)
            triggerAddPlugin(row);
    }

    void PluginBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
    {
        triggerAddPlugin(row);
    }

    void PluginBrowser::returnKeyPressed(int row)
    {
        triggerAddPlugin(row);
    }

    void PluginBrowser::changeListenerCallback(juce::ChangeBroadcaster* source)
    {
        if (source == pluginScanner.get())
            filterPlugins();
    }

    void PluginBrowser::filterPlugins()
    {
        filteredPlugins.clear();
        if (! pluginScanner)
            return;

        auto text = searchBox.getText().toLowerCase();
        auto list = pluginScanner->getDiscoveredPlugins();
        for (const auto& info : list)
        {
            auto loweredName = juce::String(info.name).toLowerCase();
            if (text.isEmpty() || loweredName.contains(text))
                filteredPlugins.push_back(info);
        }

        listBox.updateContent();
        listBox.repaint();
    }

    void PluginBrowser::triggerAddPlugin(int row)
    {
        if (row < 0 || row >= getNumRows())
            return;

        listBox.selectRow(row);

        if (onPluginChosen)
            onPluginChosen(filteredPlugins[static_cast<size_t>(row)]);
    }
}
