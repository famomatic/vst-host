#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace host::audio
{
    /** Simple per-channel resampler abstraction to allow swapping implementations later. */
    class Resampler
    {
    public:
        Resampler() = default;

        void prepare(int numChannels, double ratio)
        {
            if (channels != numChannels)
            {
                resamplers.clear();
                resamplers.reserve(static_cast<size_t>(numChannels));
                for (int i = 0; i < numChannels; ++i)
                    resamplers.emplace_back(std::make_unique<juce::LagrangeInterpolator>());
                channels = numChannels;
            }

            resampleRatio = ratio;
            for (auto& r : resamplers)
                r->reset();
        }

        void reset()
        {
            for (auto& r : resamplers)
                r->reset();
        }

        void process(const float* const* input, int numInputSamples, float* const* output, int numOutputSamples)
        {
            JUCE_ASSERT_MESSAGE_MANAGER_ONLY(jassert(resampleRatio > 0.0));
            if (resamplers.empty())
                return;

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* in = input[ch];
                auto* out = output[ch];
                if (! in || ! out)
                    continue;

                auto& interpolator = *resamplers[static_cast<size_t>(ch)];
                interpolator.process(resampleRatio, in, out, numOutputSamples);
            }
        }

        double getRatio() const noexcept { return resampleRatio; }
        int getNumChannels() const noexcept { return channels; }

    private:
        int channels { 0 };
        double resampleRatio { 1.0 };
        std::vector<std::unique_ptr<juce::LagrangeInterpolator>> resamplers;
    };
}
