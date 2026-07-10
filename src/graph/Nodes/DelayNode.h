#pragma once

#include "graph/Node.h"
#include "graph/ParameterQueue.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <array>
#include <string>
#include <vector>

namespace host::graph::nodes
{
/// Stereo delay with time, feedback and wet/dry mix. Delay time is in
/// milliseconds and is sample-rate aware.
class DelayNode : public Node
{
public:
    DelayNode();

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& ctx) override;
    std::string name() const override { return "Delay"; }
    std::string typeId() const override { return "Delay"; }
    std::vector<NodeParameter> getParameters() const override;
    void setParameters(const std::vector<NodeParameter>& parameters) override;
    void requestParameterChange(const std::string& id, double value) override;
    void applyParameterChanges() override;

private:
    void updateDelaySamples();
    void pushChange(int index, double value);

    // One delay line per channel so >2-channel configurations do not share
    // state (which would cause feedback cross-talk). Sized in prepare().
    std::vector<juce::dsp::DelayLine<float>> delays_;
    std::atomic<float> timeMs_ { 250.0f };
    std::atomic<float> feedback_ { 0.3f };
    std::atomic<float> mix_ { 0.5f };
    double preparedSampleRate_ { 44100.0 };
    int delaySamples_ { 0 };
    bool dirty_ { true };
    // Sample-level smoothing for the wet/dry coefficient so a mix change
    // doesn't click. Feedback and delay-time are block-boundary safe.
    juce::SmoothedValue<float> mixSmoothed_;
    // Pre-computed per-sample mix ramp, applied identically to every channel
    // so L/R stay perfectly aligned during a ramp (no stereo drift).
    std::vector<float> mixRampBuffer_;
    ParameterQueue queue_;
    std::vector<ParameterQueue::Entry> drained_;
    // Index layout: 0=time,1=feedback,2=mix
    static constexpr int kParamCount = 3;
};
} // namespace host::graph::nodes
