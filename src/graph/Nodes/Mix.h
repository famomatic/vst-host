#pragma once

#include "graph/GraphEngine.h"

namespace host::graph::nodes
{
    class MixNode : public Node
    {
    public:
        void prepare(double, int) override {}

        void process(ProcessContext& context) override
        {
            // TODO: implement multi-input mixing. For now, acts as a passthrough.
            juce::ignoreUnused(context);
        }

        std::string name() const override { return "Mix"; }
    };
}
