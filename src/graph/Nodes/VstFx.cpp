#include "graph/Nodes/VstFx.h"

#include <utility>

namespace host::graph::nodes
{
    VstFxNode::VstFxNode(std::unique_ptr<host::plugin::PluginInstance> instance, std::string pluginName)
        : instance_(std::move(instance))
        , pluginName_(std::move(pluginName))
    {
    }

    void VstFxNode::prepare(double sampleRate, int blockSize)
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_ = blockSize;
        if (instance_)
            instance_->prepare(sampleRate, blockSize);
    }

    void VstFxNode::process(ProcessContext& ctx)
    {
        if (! instance_ || bypassed_.load())
            return;

        if (preparedSampleRate_ != ctx.sampleRate || preparedBlockSize_ != ctx.blockSize)
        {
            preparedSampleRate_ = ctx.sampleRate;
            preparedBlockSize_ = ctx.blockSize;
            instance_->prepare(preparedSampleRate_, preparedBlockSize_);
        }

        instance_->process(ctx.inputChannels,
                           ctx.numInputChannels,
                           ctx.outputChannels,
                           ctx.numOutputChannels,
                           ctx.numFrames);
    }

    int VstFxNode::latencySamples() const noexcept
    {
        return instance_ ? instance_->latencySamples() : 0;
    }

    std::string VstFxNode::name() const
    {
        if (! pluginName_.empty())
            return pluginName_;
        return "VST FX";
    }
} // namespace host::graph::nodes
