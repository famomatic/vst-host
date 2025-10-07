#pragma once

#include "graph/Node.h"

namespace host::graph::nodes
{
    class SplitNode : public Node
    {
    public:
        void prepare(double, int) override {}

        void process(ProcessContext& context) override
        {
            juce::ignoreUnused(context);
        }

        std::string name() const override { return "Split"; }
    };
}
