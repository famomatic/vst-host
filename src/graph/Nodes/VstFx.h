#pragma once

#include "graph/GraphEngine.h"
#include "host/PluginHost.h"

#include <atomic>
#include <memory>
#include <string>

namespace host::graph::nodes
{
    class VstFxNode : public Node
    {
    public:
        VstFxNode(std::unique_ptr<host::plugin::PluginInstance> instance, std::string pluginName = {});
        ~VstFxNode() override = default;

        void setBypassed(bool shouldBypass) noexcept { bypassed_.store(shouldBypass); }
        [[nodiscard]] bool isBypassed() const noexcept { return bypassed_.load(); }

        void prepare(double sampleRate, int blockSize) override;
        void process(ProcessContext& context) override;
        int latencySamples() const noexcept override;
        std::string name() const override;

        [[nodiscard]] host::plugin::PluginInstance* plugin() const noexcept { return instance_.get(); }

    private:
        std::unique_ptr<host::plugin::PluginInstance> instance_;
        std::atomic<bool> bypassed_ { false };
        std::string pluginName_;
        int preparedBlockSize_ { 0 };
        double preparedSampleRate_ { 0.0 };
    };
}
