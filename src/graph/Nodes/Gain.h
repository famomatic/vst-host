#pragma once

#include "graph/Node.h"

#include <atomic>

namespace host::graph::nodes
{
    class GainNode : public Node
    {
    public:
        void setGain(float newGain) noexcept { gain.store(newGain); }

        void prepare(double, int) override {}

        void process(ProcessContext& context) override
        {
            auto currentGain = gain.load();
            context.audioBuffer.applyGain(currentGain);
        }

        std::string name() const override { return "Gain"; }

    private:
        std::atomic<float> gain { 1.0f };
    };
}
