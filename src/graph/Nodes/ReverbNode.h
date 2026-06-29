#pragma once

#include "graph/Node.h"
#include "graph/ParameterQueue.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <array>
#include <string>

namespace host::graph::nodes
{
/// Algorithmic reverb based on juce::Reverb (Freeverb). Exposes room size,
/// damping, wet/dry mix and width - enough for a quick space without a
/// convolution impulse.
class ReverbNode : public Node
{
public:
    ReverbNode();

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& ctx) override;
    std::string name() const override { return "Reverb"; }
    std::string typeId() const override { return "Reverb"; }
    std::vector<NodeParameter> getParameters() const override;
    void setParameters(const std::vector<NodeParameter>& parameters) override;
    void requestParameterChange(const std::string& id, double value) override;
    void applyParameterChanges() override;

private:
    void applyParameters();
    void pushChange(int index, double value);

    juce::Reverb reverb_;
    juce::Reverb::Parameters params_;
    std::atomic<float> roomSize_ { 0.5f };
    std::atomic<float> damping_ { 0.5f };
    std::atomic<float> wetLevel_ { 0.33f };
    std::atomic<float> dryLevel_ { 0.4f };
    std::atomic<float> width_ { 1.0f };
    std::atomic<bool> frozen_ { false };
    bool dirty_ { true };
    ParameterQueue queue_;
    std::vector<ParameterQueue::Entry> drained_;
    // Index layout: 0=roomSize,1=damping,2=wet,3=dry,4=width,5=freeze
    static constexpr int kParamCount = 6;
};
} // namespace host::graph::nodes
