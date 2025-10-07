#pragma once

#include "graph/Node.h"

namespace host::graph::nodes
{
    class AudioOutNode : public Node
    {
    public:
        void prepare(double, int) override {}

        void process(ProcessContext&) override {}

        std::string name() const override { return "Audio Out"; }
    };
}
