#include "graph/Nodes/GainNode.h"

#include <algorithm>

namespace host::graph::nodes
{
    namespace
    {
        // Stable parameter id list mirroring getParameters(). Index into this
        // array is what the queue stores. Layout: 0=gain.
        const std::array<std::string, 1> kParamIds { "gain" };
    }

void GainNode::prepare(double sampleRate, int blockSize)
{
    (void) sampleRate;
    (void) blockSize;
    gainSmoothed_.reset(sampleRate > 0.0 ? sampleRate : 44100.0, 0.02); // ~20 ms ramp
    gainSmoothed_.setCurrentAndTargetValue(gain_.load());
    rampBuffer_.assign(static_cast<size_t>(std::max(1, blockSize)), 0.0f);
    queue_.prepare(GainNode::kParamCount * 2);
    drained_.reserve(GainNode::kParamCount * 2);
}

void GainNode::process(ProcessContext& ctx)
{
    // Drain pending parameter changes at the block boundary so a macro/preset
    // switch lands cleanly here rather than mid-block.
    applyParameterChanges();

    // Sample-accurate gain ramp: handles both live UI dragging and the
    // discontinuity from a macro recall that sets a very different gain.
    gainSmoothed_.setTargetValue(gain_.load());
    const int frames = std::max(0, ctx.numFrames);
    const int inputs = std::max(0, ctx.numInputChannels);
    const int outputs = std::max(0, ctx.numOutputChannels);

    if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
        return;

    // Compute the ramp once for this block so every channel gets identical
    // per-sample gain values (no L/R drift). Previously getNextValue() was
    // called inside the per-channel loop, so ch1 advanced the ramp further.
    if (static_cast<int>(rampBuffer_.size()) < frames)
        rampBuffer_.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        rampBuffer_[static_cast<size_t>(i)] = gainSmoothed_.getNextValue();

    for (int outCh = 0; outCh < outputs; ++outCh)
    {
       float* dest = ctx.outputChannels[outCh];
        if (dest == nullptr)
            continue;

        // Copy input into output first, since the engine now keeps in/out
        // buffers separate.
        if (inputs > 0 && ctx.inputChannels != nullptr)
        {
            const int srcCh = outCh % inputs;
            const float* src = ctx.inputChannels[srcCh];
            if (src != nullptr)
                juce::FloatVectorOperations::copy(dest, src, frames);
            else
                juce::FloatVectorOperations::clear(dest, frames);
        }
        else
        {
            juce::FloatVectorOperations::clear(dest, frames);
        }

        for (int i = 0; i < frames; ++i)
            dest[i] *= rampBuffer_[static_cast<size_t>(i)];
    }
}

std::vector<NodeParameter> GainNode::getParameters() const
{
    const float g = gain_.load();
    return {
        { "gain", "Gain", g, 0.0, 4.0, 1.0, true },
    };
}

void GainNode::setParameters(const std::vector<NodeParameter>& parameters)
{
    // Load-time path: write state directly. requestParameterChange is the
    // live, queue-routed path used by macro recall.
    for (const auto& p : parameters)
    {
        if (p.id == "gain")
            gain_.store(static_cast<float>(p.value));
    }
}

void GainNode::pushChange(int index, double value)
{
    queue_.push(static_cast<std::size_t>(index), value);
}

void GainNode::requestParameterChange(const std::string& id, double value)
{
    if (id == "gain")
        pushChange(0, value);
}

void GainNode::applyParameterChanges()
{
    queue_.drain(drained_);
    // Single parameter: any drained entry is the gain value.
    for (const auto& entry : drained_)
    {
        gain_.store(static_cast<float>(entry.value));
    }
}
} // namespace host::graph::nodes
