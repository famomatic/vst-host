#pragma once

#include "graph/GraphEngine.h"
#include "host/PluginHost.h"

namespace host::graph::nodes
{
    class VstFxNode : public Node
    {
    public:
        explicit VstFxNode(std::shared_ptr<host::plugin::PluginInstance> instance)
            : plugin(std::move(instance))
        {
        }

        void prepare(double sampleRate, int blockSize) override
        {
            if (plugin)
                plugin->prepare(sampleRate, blockSize);
        }

        void process(ProcessContext& context) override
        {
            if (plugin)
                plugin->process(context.audioBuffer);
        }

        int latencySamples() const noexcept override
        {
            return plugin ? plugin->getLatencySamples() : 0;
        }

        std::string name() const override
        {
            return plugin ? plugin->getName() : "VST FX";
        }

        std::shared_ptr<host::plugin::PluginInstance> getPlugin() const noexcept { return plugin; }

    private:
        std::shared_ptr<host::plugin::PluginInstance> plugin;
    };
}
