#include "host/PluginHost.h"

#include <utility>

namespace host::plugin
{
    VST3PluginInstance::VST3PluginInstance(std::unique_ptr<juce::AudioPluginInstance> instance)
        : plugin(std::move(instance))
    {
    }

    void VST3PluginInstance::prepare(double sampleRate, int blockSize)
    {
        if (plugin)
            plugin->prepareToPlay(sampleRate, blockSize);
    }

    void VST3PluginInstance::process(juce::AudioBuffer<float>& buffer)
    {
        if (! plugin)
            return;

        juce::MidiBuffer midi;
        plugin->processBlock(buffer, midi);
    }

    int VST3PluginInstance::getLatencySamples() const noexcept
    {
        return plugin ? plugin->getLatencySamples() : 0;
    }

    std::string VST3PluginInstance::getName() const
    {
        return plugin ? plugin->getName().toStdString() : std::string{};
    }

    juce::MemoryBlock VST3PluginInstance::getState() const
    {
        juce::MemoryBlock block;
        if (plugin)
            plugin->getStateInformation(block);
        return block;
    }

    void VST3PluginInstance::setState(const void* data, size_t size)
    {
        if (plugin)
            plugin->setStateInformation(data, static_cast<int>(size));
    }

    std::shared_ptr<PluginInstance> loadPlugin(const PluginInfo& info, juce::AudioPluginFormatManager& formatManager, juce::String& error)
    {
        error.clear();

        juce::PluginDescription desc;
        desc.fileOrIdentifier = info.path;
        desc.name = info.name;
        desc.pluginFormatName = info.format == PluginFormat::VST3 ? "VST3" : "VST";

        std::unique_ptr<juce::AudioPluginInstance> instance(formatManager.createPluginInstance(desc, 48000.0, 512, error));
        if (! instance)
            return nullptr;

        if (info.format == PluginFormat::VST3)
            return std::make_shared<VST3PluginInstance>(std::move(instance));

    #if ENABLE_VST2
        // TODO: wrap VST2 instance into a PluginInstance implementation.
        juce::ignoreUnused(error);
        return std::make_shared<VST3PluginInstance>(std::move(instance));
    #else
        juce::ignoreUnused(instance);
        error = "VST2 support disabled";
        return nullptr;
    #endif
    }
}
