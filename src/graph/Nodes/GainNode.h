#pragma once

#include "graph/Node.h"

#include <atomic>

namespace host::graph::nodes
{
class GainNode : public Node
{
public:
    void setGain(float newGain) noexcept { gain_.store(newGain); }

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& ctx) override;

private:
    std::atomic<float> gain_ { 1.0f };
};
} // namespace host::graph::nodes
