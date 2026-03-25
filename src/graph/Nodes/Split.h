#pragma once

#include "graph/Node.h"

namespace host::graph::nodes
{
    class SplitNode : public Node
    {
    public:
        void prepare(double, int) override {}

        void process(ProcessContext& context) override
        {
            const int frames = juce::jmax(0, context.numFrames);
            const int inputs = juce::jmax(0, context.numInputChannels);
            const int outputs = juce::jmax(0, context.numOutputChannels);

            if (frames == 0 || outputs == 0 || context.outputChannels == nullptr)
                return;

            if (inputs == 0 || context.inputChannels == nullptr)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    if (auto* dest = context.outputChannels[outCh])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            for (int outCh = 0; outCh < outputs; ++outCh)
            {
                auto* dest = context.outputChannels[outCh];
                if (dest == nullptr)
                    continue;

                const int sourceChannel = outCh % inputs;
                const auto* source = context.inputChannels[sourceChannel];
                if (source == nullptr)
                {
                    juce::FloatVectorOperations::clear(dest, frames);
                    continue;
                }

                if (source != dest)
                    juce::FloatVectorOperations::copy(dest, source, frames);
            }
        }

        std::string name() const override { return "Split"; }
    };
}
