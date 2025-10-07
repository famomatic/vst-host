#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace host::plugin
{
    enum class PluginFormat
    {
        VST3,
        VST2
    };

    struct PluginInfo
    {
        std::string id;
        std::string name;
        PluginFormat format { PluginFormat::VST3 };
        std::filesystem::path path;
        int ins = 2;
        int outs = 2;
        int latency = 0;
        std::string category;
    };

    class PluginInstance
    {
    public:
        virtual ~PluginInstance() = default;
        virtual void prepare(double sr, int block) = 0;
        virtual void process(const float* const* in, int inCh, float* const* out, int outCh, int numFrames) = 0;
        [[nodiscard]] virtual int latencySamples() const = 0;
        virtual bool getState(std::vector<std::uint8_t>& out) = 0;
        virtual bool setState(const std::uint8_t* data, std::size_t len) = 0;
    };

    std::unique_ptr<PluginInstance> loadVst3(const PluginInfo& info);
    std::unique_ptr<PluginInstance> loadVst2(const PluginInfo& info);
}
