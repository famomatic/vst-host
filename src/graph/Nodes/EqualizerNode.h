#pragma once

#include "graph/Node.h"
#include "graph/ParameterQueue.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace host::graph::nodes
{
/// Four-band parametric EQ. Each band exposes frequency, gain (dB) and Q,
/// giving a usable corrective/creative tone shaper without pulling in a
/// full VST. Implemented on juce::dsp::IIR filters for stability.
class EqualizerNode : public Node
{
public:
    static constexpr int kBandCount = 4;

    struct Band
    {
        float frequency { 100.0f };
        float gainDb { 0.0f };
        float q { 0.707f };
        bool enabled { true };
    };

    EqualizerNode();

    void setBand(int index, const Band& band);
    Band getBand(int index) const;

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& ctx) override;
    std::string name() const override { return "Equalizer"; }
    std::string typeId() const override { return "Equalizer"; }
    std::vector<NodeParameter> getParameters() const override;
    void setParameters(const std::vector<NodeParameter>& parameters) override;
    void requestParameterChange(const std::string& id, double value) override;
    void applyParameterChanges() override;

private:
    void updateFilters();
    void pushChange(int index, double value);
    int parameterIndex(const std::string& id) const;

    std::array<Band, kBandCount> bands_;
    std::array<juce::dsp::IIR::Filter<float>, kBandCount> filters_;
    double preparedSampleRate_ { 44100.0 };
    bool dirty_ { true };
    ParameterQueue queue_;
    std::vector<ParameterQueue::Entry> drained_;
    std::vector<std::string> paramIds_;
};
} // namespace host::graph::nodes
