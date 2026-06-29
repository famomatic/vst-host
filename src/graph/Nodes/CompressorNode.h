#pragma once

#include "graph/Node.h"
#include "graph/ParameterQueue.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace host::graph::nodes
{
/// Simple peak compressor with threshold/ratio/attack/release and an
/// optional stereo link. Exposed parameters keep it preset-friendly.
class CompressorNode : public Node
{
public:
    CompressorNode();

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& ctx) override;
    std::string name() const override { return "Compressor"; }
    std::string typeId() const override { return "Compressor"; }
    std::vector<NodeParameter> getParameters() const override;
    void setParameters(const std::vector<NodeParameter>& parameters) override;
    void requestParameterChange(const std::string& id, double value) override;
    void applyParameterChanges() override;

private:
    void applyConfig();
    void pushChange(int index, double value);

    juce::dsp::Compressor<float> compressor_;
    std::atomic<float> threshold_ { -20.0f };
    std::atomic<float> ratio_ { 4.0f };
    std::atomic<float> attack_ { 10.0f };
    std::atomic<float> release_ { 100.0f };
    std::atomic<bool> stereoLink_ { true };
    bool dirty_ { true };
    ParameterQueue queue_;
    std::vector<ParameterQueue::Entry> drained_;
    // Index layout: 0=threshold,1=ratio,2=attack,3=release,4=stereoLink
};
} // namespace host::graph::nodes
