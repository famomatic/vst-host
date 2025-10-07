#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace host::persist
{
    struct EngineSettings
    {
        double sampleRate { 48000.0 };
        int blockSize { 256 };
    };

    class Config
    {
    public:
        Config() = default;

        void setEngineSettings(const EngineSettings& settings) { engineSettings = settings; }
        EngineSettings getEngineSettings() const noexcept { return engineSettings; }

        void setPluginDirectories(const std::vector<juce::File>& dirs);
        const std::vector<juce::File>& getPluginDirectories() const noexcept { return pluginDirectories; }

        bool load(const juce::File& file);
        bool save(const juce::File& file) const;

    private:
        EngineSettings engineSettings;
        std::vector<juce::File> pluginDirectories;
    };
}
