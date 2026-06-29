#include "graph/Nodes/CompressorNode.h"

#include <array>

namespace host::graph::nodes
{
    namespace
    {
        // Stable parameter id list mirroring getParameters(). Index into this
        // array is what the queue stores, so the audio thread applies a change
        // without string parsing. Layout: 0=threshold,1=ratio,2=attack,
        // 3=release,4=stereoLink.
        const std::array<std::string, 5> kParamIds { "threshold", "ratio", "attack", "release", "stereoLink" };
    }

    CompressorNode::CompressorNode()
    {
        applyConfig();
    }

    void CompressorNode::prepare(double sampleRate, int blockSize)
    {
        juce::dsp::ProcessSpec spec { sampleRate > 0.0 ? sampleRate : 44100.0,
                                      static_cast<juce::uint32>(std::max(1, blockSize)),
                                      2 };
        compressor_.prepare(spec);
        queue_.prepare(static_cast<int>(kParamIds.size()) * 2);
        drained_.reserve(kParamIds.size() * 2);
        applyConfig();
    }

    void CompressorNode::process(ProcessContext& ctx)
    {
        applyParameterChanges();

        const int frames = std::max(0, ctx.numFrames);
        const int inputs = std::max(0, ctx.numInputChannels);
        const int outputs = std::max(0, ctx.numOutputChannels);

        if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
            return;

        if (dirty_)
            applyConfig();

        // Copy input to output first, then run the compressor over the
        // interleaved block so the sidechain sees both channels when linked.
        for (int ch = 0; ch < outputs; ++ch)
        {
            float* dest = ctx.outputChannels[ch];
            if (dest == nullptr)
                continue;

            const float* src = (inputs > 0 && ctx.inputChannels != nullptr)
                ? ctx.inputChannels[ch % inputs]
                : nullptr;

            if (src != nullptr)
                juce::FloatVectorOperations::copy(dest, src, frames);
            else
                juce::FloatVectorOperations::clear(dest, frames);
        }

        juce::dsp::AudioBlock<float> block(ctx.outputChannels, static_cast<size_t>(outputs), static_cast<size_t>(frames));
        juce::dsp::ProcessContextReplacing<float> context(block);
        compressor_.process(context);
    }

    void CompressorNode::pushChange(int index, double value)
    {
        queue_.push(static_cast<std::size_t>(index), value);
    }

    void CompressorNode::requestParameterChange(const std::string& id, double value)
    {
        for (size_t i = 0; i < kParamIds.size(); ++i)
            if (kParamIds[i] == id)
            {
                pushChange(static_cast<int>(i), value);
                return;
            }
    }

    void CompressorNode::applyParameterChanges()
    {
        queue_.drain(drained_);
        if (drained_.empty())
            return;

        bool changed = false;
        for (const auto& entry : drained_)
        {
            const auto idx = static_cast<int>(entry.idHash);
            if (idx < 0 || idx >= static_cast<int>(kParamIds.size()))
                continue;

            switch (idx)
            {
                case 0: threshold_.store(static_cast<float>(entry.value)); changed = true; break;
                case 1: ratio_.store(static_cast<float>(entry.value)); changed = true; break;
                case 2: attack_.store(static_cast<float>(entry.value)); changed = true; break;
                case 3: release_.store(static_cast<float>(entry.value)); changed = true; break;
                case 4: stereoLink_.store(entry.value > 0.5); changed = true; break;
                default: break;
            }
        }
        if (changed)
            dirty_ = true;
    }

    void CompressorNode::applyConfig()
    {
        compressor_.setThreshold(threshold_.load());
        compressor_.setRatio(ratio_.load());
        compressor_.setAttack(attack_.load());
        compressor_.setRelease(release_.load());
        dirty_ = false;
    }

    std::vector<NodeParameter> CompressorNode::getParameters() const
    {
        return {
            { "threshold", "Threshold", threshold_.load(), -60.0, 0.0, -20.0, true },
            { "ratio", "Ratio", ratio_.load(), 1.0, 20.0, 4.0, true },
            { "attack", "Attack", attack_.load(), 0.1, 100.0, 10.0, true },
            { "release", "Release", release_.load(), 10.0, 1000.0, 100.0, true },
            { "stereoLink", "Stereo Link", stereoLink_.load() ? 1.0 : 0.0, 0.0, 1.0, 1.0, false },
        };
    }

    void CompressorNode::setParameters(const std::vector<NodeParameter>& parameters)
    {
        bool changed = false;
        for (const auto& p : parameters)
        {
            if (p.id == "threshold") { threshold_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "ratio") { ratio_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "attack") { attack_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "release") { release_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "stereoLink") { stereoLink_.store(p.value > 0.5); changed = true; }
        }
        if (changed)
            dirty_ = true;
    }
} // namespace host::graph::nodes
