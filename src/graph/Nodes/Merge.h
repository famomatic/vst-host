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

            if (context.inputChannels == nullptr || inputs == 0 || frames > preparedBlockSize_)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    if (auto* dest = context.outputChannels[outCh])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            std::fill(monoScratch_.begin(), monoScratch_.begin() + frames, 0.0f);

            int contributors = 0;
            for (int inCh = 0; inCh < inputs; ++inCh)
            {
                const auto* source = context.inputChannels[inCh];
                if (source == nullptr)
                    continue;

                juce::FloatVectorOperations::add(monoScratch_.data(), source, frames);
                ++contributors;
            }

            if (contributors == 0)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    if (auto* dest = context.outputChannels[outCh])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            if (contributors > 1)
            {
                const float gain = 1.0f / static_cast<float>(contributors);
                juce::FloatVectorOperations::multiply(monoScratch_.data(), gain, frames);
            }

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
