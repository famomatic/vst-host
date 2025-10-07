#include "gui/GraphView.h"

namespace host::gui
{
    GraphView::GraphView()
    {
        setInterceptsMouseClicks(true, true);
    }

    void GraphView::setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine)
    {
        graph = std::move(graphEngine);
        repaint();
    }

    void GraphView::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::darkgrey);

        if (! graph)
        {
            g.setColour(juce::Colours::white);
            g.setFont(16.0f);
            g.drawText("Graph is empty", getLocalBounds(), juce::Justification::centred);
            return;
        }

        auto schedule = graph->getSchedule();
        const auto nodeWidth = 140;
        const auto nodeHeight = 60;
        const auto spacing = 20;

        g.setFont(14.0f);
        int x = spacing;
        int y = getHeight() / 2 - nodeHeight / 2;

        for (auto& id : schedule)
        {
            if (auto* node = graph->getNode(id))
            {
                juce::Rectangle<int> bounds(x, y, nodeWidth, nodeHeight);
                g.setColour(juce::Colours::black.withAlpha(0.6f));
                g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
                g.setColour(juce::Colours::orange);
                g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 2.0f);
                g.setColour(juce::Colours::white);
                g.drawFittedText(juce::String(node->name()), bounds.reduced(10), juce::Justification::centred, 2);
                x += nodeWidth + spacing;
            }
        }
    }

    void GraphView::resized()
    {
    }
}
