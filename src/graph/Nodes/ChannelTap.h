#pragma once

#include "graph/Node.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <string>
#include <vector>

namespace host::graph::nodes
{
    /// Selects a single input channel and forwards it to every output
    /// channel. Used to tap the Left or Right leg out of a Split node so a
    /// stereo signal can be processed through independent mono chains and
    /// later recombined by Merge.
    class ChannelTapNode : public Node
    {
    public:
        explicit ChannelTapNode(int channel = 0) : channel_(channel) {}

        void prepare(double, int) override {}

        void process(ProcessContext& context) override
        {
            const int frames = std::max(0, context.numFrames);
            const int inputs = std::max(0, context.numInputChannels);
            const int outputs = std::max(0, context.numOutputChannels);

            if (frames == 0 || outputs == 0 || context.outputChannels == nullptr)
                return;

            // No input available: emit silence on every output channel.
            if (inputs == 0 || context.inputChannels == nullptr)
            {
                for (int outCh = 0; outCh < outputs; ++outCh)
                {
                    if (auto* dest = context.outputChannels[outCh])
                        juce::FloatVectorOperations::clear(dest, frames);
                }
                return;
            }

            const int sourceChannel = std::clamp(channel_, 0, inputs - 1);
            const auto* source = context.inputChannels[sourceChannel];

            for (int outCh = 0; outCh < outputs; ++outCh)
            {
                auto* dest = context.outputChannels[outCh];
                if (dest == nullptr)
                    continue;

                if (source != nullptr)
                    juce::FloatVectorOperations::copy(dest, source, frames);
                else
                    juce::FloatVectorOperations::clear(dest, frames);
            }
        }

        std::string name() const override { return "Channel Tap"; }

        std::string typeId() const override { return "ChannelTap"; }

        std::vector<NodeParameter> getParameters() const override
        {
            return { NodeParameter { "channel", "Channel", static_cast<double>(channel_), 0.0, 31.0,
                                     static_cast<double>(channel_), false } };
        }

        void setParameters(const std::vector<NodeParameter>& parameters) override
        {
            for (const auto& p : parameters)
            {
                if (p.id == "channel")
                    channel_ = std::clamp(static_cast<int>(p.value), 0, 31);
            }
        }

    private:
        int channel_ { 0 };
    };
}