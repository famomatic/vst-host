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

    void VstFxNode::process(ProcessContext& context)
    {
        if (! instance_ || bypassed_.load())
            return;

        auto& buffer = context.audioBuffer;
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        if (preparedSampleRate_ != context.sampleRate || preparedBlockSize_ != context.blockSize)
        {
            preparedSampleRate_ = context.sampleRate;
            preparedBlockSize_ = context.blockSize;
            instance_->prepare(preparedSampleRate_, preparedBlockSize_);
        }

        auto* const* inputData = buffer.getArrayOfReadPointers();
        auto* const* outputData = buffer.getArrayOfWritePointers();
        instance_->process(inputData, numChannels, outputData, numChannels, numSamples);
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
}
