#include "graph/Nodes/VstFx.h"

#include <utility>

namespace host::graph::nodes
{
    VstFxNode::VstFxNode(std::unique_ptr<host::plugin::PluginInstance> instance,
                         std::string pluginName,
                         std::optional<host::plugin::PluginInfo> pluginInfo)
        : instance_(std::move(instance))
        , pluginName_(std::move(pluginName))
    {
        if (pluginInfo.has_value())
            pluginInfo_ = std::move(pluginInfo);

        if (pluginName_.empty() && pluginInfo_.has_value())
            pluginName_ = pluginInfo_->name;
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

        if (pluginInfo_.has_value() && ! pluginInfo_->name.empty())
            return pluginInfo_->name;

        return "VST FX";
    }

    void VstFxNode::setDisplayName(std::string newName)
    {
        pluginName_ = std::move(newName);
    }

    void VstFxNode::setPluginInfo(host::plugin::PluginInfo info)
    {
        pluginInfo_ = std::move(info);
        if (pluginName_.empty() && pluginInfo_.has_value())
            pluginName_ = pluginInfo_->name;
    }
} // namespace host::graph::nodes
