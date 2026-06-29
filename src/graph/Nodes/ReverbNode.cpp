#include "graph/Nodes/ReverbNode.h"

namespace host::graph::nodes
{
    namespace
    {
        // Stable parameter id list mirroring getParameters(). Index into this
        // array is what the queue stores. Layout: 0=roomSize,1=damping,
        // 2=wet,3=dry,4=width,5=freeze.
        const std::array<std::string, 6> kParamIds {
            "roomSize", "damping", "wet", "dry", "width", "freeze"
        };
    }

    ReverbNode::ReverbNode()
    {
        applyParameters();
    }

    void ReverbNode::prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        reverb_.setSampleRate(sampleRate > 0.0 ? sampleRate : 44100.0);
        queue_.prepare(ReverbNode::kParamCount * 2);
        drained_.reserve(ReverbNode::kParamCount * 2);
        applyParameters();
    }

    void ReverbNode::process(ProcessContext& ctx)
    {
        applyParameterChanges();

        const int frames = std::max(0, ctx.numFrames);
        const int inputs = std::max(0, ctx.numInputChannels);
        const int outputs = std::max(0, ctx.numOutputChannels);

        if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
            return;

        if (dirty_)
            applyParameters();

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

        // juce::Reverb processes stereo in place. When only one channel is
        // present we duplicate to a stereo scratch so the reverb tail behaves.
        if (outputs >= 2)
        {
            reverb_.processStereo(ctx.outputChannels[0], ctx.outputChannels[1], frames);
        }
        else if (outputs == 1 && ctx.outputChannels[0] != nullptr)
        {
            std::vector<float> temp(static_cast<size_t>(frames), 0.0f);
            reverb_.processStereo(ctx.outputChannels[0], temp.data(), frames);
        }
    }

    void ReverbNode::applyParameters()
    {
        params_.roomSize = roomSize_.load();
        params_.damping = damping_.load();
        params_.wetLevel = wetLevel_.load();
        params_.dryLevel = dryLevel_.load();
        params_.width = width_.load();
        params_.freezeMode = frozen_.load() ? 1.0f : 0.0f;
        reverb_.setParameters(params_);
        dirty_ = false;
    }

    void ReverbNode::pushChange(int index, double value)
    {
        queue_.push(static_cast<std::size_t>(index), value);
    }

    void ReverbNode::requestParameterChange(const std::string& id, double value)
    {
        for (int i = 0; i < ReverbNode::kParamCount; ++i)
            if (kParamIds[static_cast<size_t>(i)] == id)
            {
                pushChange(i, value);
                return;
            }
    }

    void ReverbNode::applyParameterChanges()
    {
        queue_.drain(drained_);
        if (drained_.empty())
            return;

        bool changed = false;
        for (const auto& entry : drained_)
        {
            const auto idx = static_cast<int>(entry.idHash);
            if (idx < 0 || idx >= ReverbNode::kParamCount)
                continue;

            switch (idx)
            {
                case 0: roomSize_.store(static_cast<float>(entry.value)); changed = true; break;
                case 1: damping_.store(static_cast<float>(entry.value)); changed = true; break;
                case 2: wetLevel_.store(static_cast<float>(entry.value)); changed = true; break;
                case 3: dryLevel_.store(static_cast<float>(entry.value)); changed = true; break;
                case 4: width_.store(static_cast<float>(entry.value)); changed = true; break;
                case 5: frozen_.store(entry.value > 0.5); changed = true; break;
                default: break;
            }
        }
        if (changed)
            dirty_ = true;
    }

    std::vector<NodeParameter> ReverbNode::getParameters() const
    {
        return {
            { "roomSize", "Room Size", roomSize_.load(), 0.0, 1.0, 0.5, true },
            { "damping", "Damping", damping_.load(), 0.0, 1.0, 0.5, true },
            { "wet", "Wet", wetLevel_.load(), 0.0, 1.0, 0.33, true },
            { "dry", "Dry", dryLevel_.load(), 0.0, 1.0, 0.4, true },
            { "width", "Width", width_.load(), 0.0, 1.0, 1.0, true },
            { "freeze", "Freeze", frozen_.load() ? 1.0 : 0.0, 0.0, 1.0, 0.0, false },
        };
    }

    void ReverbNode::setParameters(const std::vector<NodeParameter>& parameters)
    {
        bool changed = false;
        for (const auto& p : parameters)
        {
            if (p.id == "roomSize") { roomSize_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "damping") { damping_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "wet") { wetLevel_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "dry") { dryLevel_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "width") { width_.store(static_cast<float>(p.value)); changed = true; }
            else if (p.id == "freeze") { frozen_.store(p.value > 0.5); changed = true; }
        }
        if (changed)
            dirty_ = true;
    }
} // namespace host::graph::nodes
