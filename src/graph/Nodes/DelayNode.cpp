#include "graph/Nodes/DelayNode.h"

#include <algorithm>
#include <cmath>

namespace host::graph::nodes
{
    namespace
    {
        // Stable parameter id list mirroring getParameters(). Index into this
        // array is what the queue stores. Layout: 0=time,1=feedback,2=mix.
        const std::array<std::string, 3> kParamIds { "time", "feedback", "mix" };
    }

    DelayNode::DelayNode()
    {
        updateDelaySamples();
    }

    void DelayNode::prepare(double sampleRate, int blockSize)
    {
        preparedSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        juce::dsp::ProcessSpec spec { preparedSampleRate_, static_cast<juce::uint32>(std::max(1, blockSize)), 2 };
        for (auto& d : delays_)
        {
            d.prepare(spec);
            d.reset();
        }
        mixSmoothed_.reset(preparedSampleRate_, 0.02); // ~20 ms ramp
        mixSmoothed_.setCurrentAndTargetValue(std::clamp(mix_.load(), 0.0f, 1.0f));
        queue_.prepare(DelayNode::kParamCount * 2);
        drained_.reserve(DelayNode::kParamCount * 2);
        updateDelaySamples();
    }

    void DelayNode::process(ProcessContext& ctx)
    {
        applyParameterChanges();

        const int frames = std::max(0, ctx.numFrames);
        const int inputs = std::max(0, ctx.numInputChannels);
        const int outputs = std::max(0, ctx.numOutputChannels);

        if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
            return;

        if (dirty_)
            updateDelaySamples();

        const float feedback = std::clamp(feedback_.load(), 0.0f, 0.99f);
        // Track the mix target each block; the smoothed value ramps sample by
        // sample so a macro/preset switch doesn't click at the wet/dry edge.
        mixSmoothed_.setTargetValue(std::clamp(mix_.load(), 0.0f, 1.0f));

        for (int ch = 0; ch < outputs; ++ch)
        {
            float* dest = ctx.outputChannels[ch];
            if (dest == nullptr)
                continue;

            const float* src = (inputs > 0 && ctx.inputChannels != nullptr)
                ? ctx.inputChannels[ch % inputs]
                : nullptr;

            if (src == nullptr)
            {
                juce::FloatVectorOperations::clear(dest, frames);
                continue;
            }

            auto& delay = delays_[static_cast<size_t>(ch % 2)];

            for (int i = 0; i < frames; ++i)
            {
                const float input = src[i];
                const float delayed = delay.popSample(0);
                const float wet = mixSmoothed_.getNextValue();
                const float dry = 1.0f - wet;
                const float output = input * dry + delayed * wet;
                dest[i] = output;
                delay.pushSample(0, output + delayed * feedback);
            }
            // Re-run the ramp for subsequent channels so each channel starts
            // from the current target rather than the just-finished ramp end.
            mixSmoothed_.setTargetValue(mixSmoothed_.getTargetValue());
        }
    }

    void DelayNode::pushChange(int index, double value)
    {
        queue_.push(static_cast<std::size_t>(index), value);
    }

    void DelayNode::requestParameterChange(const std::string& id, double value)
    {
        for (int i = 0; i < DelayNode::kParamCount; ++i)
            if (kParamIds[static_cast<size_t>(i)] == id)
            {
                pushChange(i, value);
                return;
            }
    }

    void DelayNode::applyParameterChanges()
    {
        queue_.drain(drained_);
        if (drained_.empty())
            return;

        bool changed = false;
        for (const auto& entry : drained_)
        {
            const auto idx = static_cast<int>(entry.idHash);
            if (idx < 0 || idx >= DelayNode::kParamCount)
                continue;

            switch (idx)
            {
                case 0: timeMs_.store(static_cast<float>(entry.value)); changed = true; break;
                case 1: feedback_.store(static_cast<float>(entry.value)); changed = true; break;
                case 2: mix_.store(static_cast<float>(entry.value)); changed = true; break;
                default: break;
            }
        }
        if (changed)
            dirty_ = true;
    }

    void DelayNode::updateDelaySamples()
    {
        delaySamples_ = std::max(1, static_cast<int>(std::round(timeMs_.load() * 0.001 * preparedSampleRate_)));
        for (auto& d : delays_)
            d.setDelay(static_cast<double>(delaySamples_));
        dirty_ = false;
    }

    std::vector<NodeParameter> DelayNode::getParameters() const
    {
        return {
            { "time", "Time (ms)", timeMs_.load(), 1.0, 2000.0, 250.0, true },
            { "feedback", "Feedback", feedback_.load(), 0.0, 0.99, 0.3, true },
            { "mix", "Mix", mix_.load(), 0.0, 1.0, 0.5, true },
        };
    }

    void DelayNode::setParameters(const std::vector<NodeParameter>& parameters)
    {
        bool changed = false;
        for (const auto& p : parameters)
        {
            if (p.id == "time") { timeMs_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "feedback") { feedback_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "mix") { mix_.store(static_cast<float>(p.value)); changed = true; }
        }
        if (changed)
            dirty_ = true;
    }
} // namespace host::graph::nodes
