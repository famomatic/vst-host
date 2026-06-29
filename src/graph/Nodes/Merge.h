#pragma once

#include "graph/Node.h"

#include <algorithm>
#include <vector>

namespace host::graph::nodes
{
    class MergeNode : public Node
    {
    public:
        void prepare(double, int blockSize) override
        {
            preparedBlockSize_ = std::max(1, blockSize);
            monoScratch_.assign(static_cast<size_t>(preparedBlockSize_), 0.0f);
        }

        void process(ProcessContext& context) override
        {
            const int frames = std::max(0, context.numFrames);
            const int inputs = std::max(0, context.numInputChannels);
            const int outputs = std::max(0, context.numOutputChannels);

            if (frames == 0 || outputs == 0 || context.outputChannels == nullptr)
                return;

            if (context.inputChannels == nullptr || inputs == 0)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    if (auto* dest = context.outputChannels[outCh])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            // Multi-input merge: map input channels 1:1 onto output channels
            // so two mono chains (L + R) recombine into a stereo pair. Inputs
            // beyond the output count are summed into the last output; extra
            // outputs repeat the final input. This keeps Split -> Tap(L) /
            // Tap(R) -> chains -> Merge round-trippable while still supporting
            // the classic mono-sum behaviour when only one input is present.
            if (inputs >= 2)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    auto* dest = context.outputChannels[outCh];
                    if (dest == nullptr)
                        continue;

                    const int mappedIn = std::min(outCh, inputs - 1);
                    const auto* source = context.inputChannels[mappedIn];

                    // Sum any further inputs that also map to this output
                    // (inputs > outputs case) so nothing is silently dropped.
                    if (source != nullptr)
                        juce::FloatVectorOperations::copy(dest, source, frames);
                    else
                        juce::FloatVectorOperations::clear(dest, frames);

                    for (int extraIn = mappedIn + outputs; extraIn < inputs; extraIn += outputs)
                    {
                        const auto* extra = context.inputChannels[extraIn];
                        if (extra != nullptr)
                            juce::FloatVectorOperations::add(dest, extra, frames);
                    }
                }
                return;
            }

            // Single input: up-mix to every output (mono -> stereo), summed
            // to mono when multiple inputs collapse to one. Kept as the
            // legacy path so existing mono-sum projects behave unchanged.
            if (frames > preparedBlockSize_)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    if (auto* dest = context.outputChannels[outCh])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            std::fill(monoScratch_.begin(), monoScratch_.begin() + frames, 0.0f);

            const auto* source = context.inputChannels[0];
            if (source != nullptr)
                juce::FloatVectorOperations::copy(monoScratch_.data(), source, frames);

            for (int outCh = 0; outCh < outputs; ++outCh)
            {
                if (auto* dest = context.outputChannels[outCh])
                    juce::FloatVectorOperations::copy(dest, monoScratch_.data(), frames);
            }
        }

        std::string name() const override { return "Merge"; }

    private:
        std::vector<float> monoScratch_;
        int preparedBlockSize_ { 0 };
    };
}
