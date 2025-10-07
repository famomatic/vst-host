#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

#include "graph/GraphEngine.h"

namespace host::gui
{
    class GraphView : public juce::Component
    {
    public:
        GraphView();
        ~GraphView() override = default;

        void setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        std::shared_ptr<host::graph::GraphEngine> graph;
    };
}
