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
        if (! instance_)
            return;

        // Bypass must still forward the input to the output. Previously this
        // returned immediately, leaving the node's output buffer untouched and
        // producing garbage/silence downstream because the runtime only copies
        // the plugin output onto the node buffer when process() actually runs.
        if (bypassed_.load())
        {
            const int frames = std::max(0, ctx.numFrames);
            const int inputs = std::max(0, ctx.numInputChannels);
            const int outputs = std::max(0, ctx.numOutputChannels);
            if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
                return;

            for (int outCh = 0; outCh < outputs; ++outCh)
            {
                float* dest = ctx.outputChannels[outCh];
                if (dest == nullptr)
                    continue;

                if (inputs > 0 && ctx.inputChannels != nullptr)
                {
                    const int srcCh = outCh % inputs;
                    const float* src = ctx.inputChannels[srcCh];
                    if (src != nullptr)
                    {
                        juce::FloatVectorOperations::copy(dest, src, frames);
                        continue;
                    }
                }
                juce::FloatVectorOperations::clear(dest, frames);
            }
            return;
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

    int VstFxNode::inputChannelCount() const
    {
        if (pluginInfo_.has_value() && pluginInfo_->ins > 0)
            return pluginInfo_->ins;
        return 2;
    }

    int VstFxNode::outputChannelCount() const
    {
        if (pluginInfo_.has_value() && pluginInfo_->outs > 0)
            return pluginInfo_->outs;
        return 2;
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
