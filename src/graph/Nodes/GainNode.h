#pragma once

#include "graph/Node.h"
#include "graph/ParameterQueue.h"

#include <atomic>
#include <array>
#include <string>

namespace host::graph::nodes
{
class GainNode : public Node
{
public:
    void setGain(float newGain) noexcept { gain_.store(newGain); }
    [[nodiscard]] float getGain() const noexcept { return gain_.load(); }

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& ctx) override;
    std::string name() const override { return "Gain"; }
    std::string typeId() const override { return "Gain"; }
    std::vector<NodeParameter> getParameters() const override;
    void setParameters(const std::vector<NodeParameter>& parameters) override;
    void requestParameterChange(const std::string& id, double value) override;
    void applyParameterChanges() override;

private:
    void pushChange(int index, double value);

    std::atomic<float> gain_ { 1.0f };
    // Smoothes the gain coefficient sample by sample so macro/preset switches
    // don't click when gain is part of the captured snapshot.
    juce::SmoothedValue<float> gainSmoothed_;
    ParameterQueue queue_;
    std::vector<ParameterQueue::Entry> drained_;
    // Index layout: 0=gain
    static constexpr int kParamCount = 1;
};
} // namespace host::graph::nodes
