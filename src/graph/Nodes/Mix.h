#pragma once

#include "graph/Node.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <vector>

namespace host::graph::nodes
{
    class MixNode : public Node
    {
    public:
        void prepare(double, int blockSize) override
        {
            preparedBlockSize_ = std::max(0, blockSize);
            mixBuffer_.setSize(0, 0);
            contributions_.clear();
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
                for (int ch = 0; ch < outputs; ++ch)
                {
                    if (auto* dest = context.outputChannels[ch])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            const int requiredSamples = std::max(frames, preparedBlockSize_);
            if (mixBuffer_.getNumChannels() != outputs || mixBuffer_.getNumSamples() < requiredSamples)
                mixBuffer_.setSize(outputs, requiredSamples, false, false, true);

            mixBuffer_.clear();
            contributions_.assign(static_cast<size_t>(outputs), 0);

            for (int inCh = 0; inCh < inputs; ++inCh)
            {
                const float* source = context.inputChannels[inCh];
                if (source == nullptr)
                    continue;

                const int destChannel = outputs > 0 ? (inCh % outputs) : 0;
                auto* dest = mixBuffer_.getWritePointer(destChannel);
                juce::FloatVectorOperations::add(dest, source, frames);
                contributions_[static_cast<size_t>(destChannel)] += 1;
            }

            for (int outCh = 0; outCh < outputs; ++outCh)
            {
                float* dest = context.outputChannels[outCh];
                if (dest == nullptr)
                    continue;

                const int contributing = (outCh < static_cast<int>(contributions_.size()))
                                             ? contributions_[static_cast<size_t>(outCh)]
                                             : 0;

                if (contributing == 0)
                {
                    juce::FloatVectorOperations::clear(dest, frames);
                    continue;
                }

                const float* mixed = mixBuffer_.getReadPointer(outCh);
                juce::FloatVectorOperations::copy(dest, mixed, frames);

                if (contributing > 1)
                {
                    const float gain = 1.0f / static_cast<float>(contributing);
                    juce::FloatVectorOperations::multiply(dest, gain, frames);
                }
            }
        }

        std::string name() const override { return "Mix"; }

    private:
        juce::AudioBuffer<float> mixBuffer_;
        std::vector<int> contributions_;
        int preparedBlockSize_ { 0 };
    };
}
