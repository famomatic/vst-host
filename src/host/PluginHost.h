#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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
        std::string identifier;
        std::string name;
        PluginFormat format;
        std::string path;
        std::vector<std::string> categories;
        int numInputs { 0 };
        int numOutputs { 0 };
        int latency { 0 };
    };

    class PluginInstance
    {
    public:
        virtual ~PluginInstance() = default;
        virtual void prepare(double sampleRate, int blockSize) = 0;
        virtual void process(juce::AudioBuffer<float>& buffer) = 0;
        virtual int getLatencySamples() const noexcept = 0;
        virtual std::string getName() const = 0;
        virtual juce::MemoryBlock getState() const = 0;
        virtual void setState(const void* data, size_t size) = 0;
    };

    class VST3PluginInstance : public PluginInstance
    {
    public:
        explicit VST3PluginInstance(std::unique_ptr<juce::AudioPluginInstance> instance);

        void prepare(double sampleRate, int blockSize) override;
        void process(juce::AudioBuffer<float>& buffer) override;
        int getLatencySamples() const noexcept override;
        std::string getName() const override;
        juce::MemoryBlock getState() const override;
        void setState(const void* data, size_t size) override;

    private:
        std::unique_ptr<juce::AudioPluginInstance> plugin;
    };

    std::shared_ptr<PluginInstance> loadPlugin(const PluginInfo& info, juce::AudioPluginFormatManager& formatManager, juce::String& error);
}
