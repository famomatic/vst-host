#pragma once

#include "graph/Node.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace host::graph::nodes
{
    class AudioInNode : public Node
    {
    public:
        void prepare(double, int) override {}

        void process(ProcessContext& context) override
        {
            // Forward the host input captured by the engine into the node's
            // output buffer so downstream nodes receive it.
            const int frames = std::max(0, context.numFrames);
            const int inputs = std::max(0, context.numInputChannels);
            const int outputs = std::max(0, context.numOutputChannels);
            if (frames == 0 || outputs == 0 || context.outputChannels == nullptr)
                return;

            for (int outCh = 0; outCh < outputs; ++outCh)
            {
                float* dest = context.outputChannels[outCh];
                if (dest == nullptr)
                    continue;

                if (inputs > 0 && context.inputChannels != nullptr)
                {
                    const int srcCh = outCh % inputs;
                    const float* src = context.inputChannels[srcCh];
                    if (src != nullptr)
                    {
                        juce::FloatVectorOperations::copy(dest, src, frames);
                        continue;
                    }
                }
                juce::FloatVectorOperations::clear(dest, frames);
            }
        }

        std::string name() const override { return "Audio In"; }
    };
}
