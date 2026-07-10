#include "graph/Nodes/EqualizerNode.h"

#include <cmath>

namespace host::graph::nodes
{
namespace
{
    // Default band centres spaced across the audible spectrum.
    constexpr std::array<float, EqualizerNode::kBandCount> kDefaultFreqs { 80.0f, 500.0f, 2500.0f, 8000.0f };

    // Stable parameter id list in the same order as getParameters(). Index
    // into this list is what the queue stores, so the audio thread can apply
    // a change without string parsing or hash lookups.
    std::vector<std::string> buildParamIds()
    {
        std::vector<std::string> ids;
        ids.reserve(static_cast<size_t>(EqualizerNode::kBandCount) * 4);
        for (int b = 0; b < EqualizerNode::kBandCount; ++b)
        {
            const auto prefix = "band" + std::to_string(b);
            ids.push_back(prefix + "_freq");
            ids.push_back(prefix + "_gain");
            ids.push_back(prefix + "_q");
            ids.push_back(prefix + "_on");
        }
        return ids;
    }
    }

    EqualizerNode::EqualizerNode()
    {
        for (size_t i = 0; i < bands_.size(); ++i)
        {
            bands_[i].frequency = kDefaultFreqs[i];
            bands_[i].gainDb = 0.0f;
            bands_[i].q = 0.707f;
            bands_[i].enabled = true;
        }
        paramIds_ = buildParamIds();
    }

    void EqualizerNode::setBand(int index, const Band& band)
    {
        if (index < 0 || index >= kBandCount)
            return;
        bands_[static_cast<size_t>(index)] = band;
        dirty_ = true;
    }

    EqualizerNode::Band EqualizerNode::getBand(int index) const
    {
        if (index < 0 || index >= kBandCount)
            return {};
        return bands_[static_cast<size_t>(index)];
    }

    void EqualizerNode::prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        preparedSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        juce::dsp::ProcessSpec spec { preparedSampleRate_, static_cast<juce::uint32>(std::max(1, blockSize)), 2 };
        for (auto& bandFilters : filters_)
            for (auto& f : bandFilters)
                f.prepare(spec);
        queue_.prepare(static_cast<int>(kBandCount) * 4 * 2);
        drained_.reserve(static_cast<size_t>(kBandCount) * 4 * 2);
        updateFilters();
    }

    void EqualizerNode::process(ProcessContext& ctx)
    {
        applyParameterChanges();
        const int frames = std::max(0, ctx.numFrames);
        const int inputs = std::max(0, ctx.numInputChannels);
        const int outputs = std::max(0, ctx.numOutputChannels);

        if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
            return;

        if (dirty_)
            updateFilters();

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

           for (int b = 0; b < kBandCount; ++b)
           {
               if (bands_[static_cast<size_t>(b)].enabled)
               {
                    // IIR::Filter processes via a ProcessContext; feed it a
                    // single-channel block wrapping this channel's output.
                    float* channelPtr = dest;
                    juce::dsp::AudioBlock<float> block(&channelPtr, 1, static_cast<size_t>(frames));
                    juce::dsp::ProcessContextReplacing<float> context(block);
                    // Use the per-channel filter so L/R keep independent state.
                    const int filterCh = std::min(ch, kMaxChannels - 1);
                    filters_[static_cast<size_t>(b)][static_cast<size_t>(filterCh)].process(context);
               }
           }
        }
    }

    void EqualizerNode::updateFilters()
    {
        for (int b = 0; b < kBandCount; ++b)
        {
            const auto& band = bands_[static_cast<size_t>(b)];
            // Disabled or unity-gain bands get unity (passthrough) coefficients
            // so process() never applies stale non-unity coefficients. reset()
            // alone only clears the delay-line state, not the coefficients.
            juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
            if (! band.enabled || band.gainDb == 0.0f)
            {
                // (1, 0, 1, 0) = y[n] = x[n] (unity). Matches JUCE default.
                coeffs = juce::dsp::IIR::Coefficients<float>::Ptr(
                    new juce::dsp::IIR::Coefficients<float>(1, 0, 1, 0));
            }
            else
            {
                // makePeakFilter handles both boost (gain > 1) and cut (gain < 1).
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    preparedSampleRate_, band.frequency, band.q,
                    juce::Decibels::decibelsToGain(band.gainDb));
            }
            for (auto& f : filters_[static_cast<size_t>(b)])
            {
                f.coefficients = coeffs;
                f.reset();
            }
        }
        dirty_ = false;
    }

    std::vector<NodeParameter> EqualizerNode::getParameters() const
    {
        std::vector<NodeParameter> params;
        params.reserve(static_cast<size_t>(kBandCount) * 4);
        for (int b = 0; b < kBandCount; ++b)
        {
            const auto& band = bands_[static_cast<size_t>(b)];
            const auto prefix = "band" + std::to_string(b);
            params.push_back({ prefix + "_freq", "Band " + std::to_string(b + 1) + " Freq", band.frequency, 20.0, 20000.0, kDefaultFreqs[static_cast<size_t>(b)], true });
            params.push_back({ prefix + "_gain", "Band " + std::to_string(b + 1) + " Gain", band.gainDb, -24.0, 24.0, 0.0, true });
            params.push_back({ prefix + "_q", "Band " + std::to_string(b + 1) + " Q", band.q, 0.1, 12.0, 0.707, true });
            params.push_back({ prefix + "_on", "Band " + std::to_string(b + 1) + " On", band.enabled ? 1.0 : 0.0, 0.0, 1.0, 1.0, false });
        }
        return params;
    }

    void EqualizerNode::setParameters(const std::vector<NodeParameter>& parameters)
    {
        // Load-time path: the audio stream may not be running yet, so write
        // band state directly and flag the filters dirty. requestParameterChange
        // is the live path that routes through the lock-free queue.
        for (const auto& p : parameters)
        {
            for (int b = 0; b < kBandCount; ++b)
            {
                const auto prefix = "band" + std::to_string(b);
                if (p.id == prefix + "_freq") { bands_[static_cast<size_t>(b)].frequency = static_cast<float>(p.value); }
                else if (p.id == prefix + "_gain") { bands_[static_cast<size_t>(b)].gainDb = static_cast<float>(p.value); }
                else if (p.id == prefix + "_q") { bands_[static_cast<size_t>(b)].q = static_cast<float>(p.value); }
                else if (p.id == prefix + "_on") { bands_[static_cast<size_t>(b)].enabled = p.value > 0.5; }
            }
        }
        dirty_ = true;
    }

    int EqualizerNode::parameterIndex(const std::string& id) const
    {
        for (size_t i = 0; i < paramIds_.size(); ++i)
            if (paramIds_[i] == id)
                return static_cast<int>(i);
        return -1;
    }

    void EqualizerNode::pushChange(int index, double value)
    {
        queue_.push(static_cast<std::size_t>(index), value);
    }

    void EqualizerNode::requestParameterChange(const std::string& id, double value)
    {
        const auto idx = parameterIndex(id);
        if (idx >= 0)
            pushChange(idx, value);
    }

    void EqualizerNode::applyParameterChanges()
    {
        queue_.drain(drained_);
        if (drained_.empty())
            return;

        bool changed = false;
        for (const auto& entry : drained_)
        {
            const auto idx = static_cast<int>(entry.idHash);
            if (idx < 0 || idx >= static_cast<int>(paramIds_.size()))
                continue;
            const auto& id = paramIds_[static_cast<size_t>(idx)];

            for (int b = 0; b < kBandCount; ++b)
            {
                const auto prefix = "band" + std::to_string(b);
                if (id == prefix + "_freq") { bands_[static_cast<size_t>(b)].frequency = static_cast<float>(entry.value); changed = true; }
                else if (id == prefix + "_gain") { bands_[static_cast<size_t>(b)].gainDb = static_cast<float>(entry.value); changed = true; }
                else if (id == prefix + "_q") { bands_[static_cast<size_t>(b)].q = static_cast<float>(entry.value); changed = true; }
                else if (id == prefix + "_on") { bands_[static_cast<size_t>(b)].enabled = entry.value > 0.5; changed = true; }
            }
        }
        if (changed)
            dirty_ = true;
    }
} // namespace host::graph::nodes
