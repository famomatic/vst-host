#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace host::persist
{
    struct EngineSettings
    {
        double sampleRate { 48000.0 };
        int blockSize { 256 };
        // Resampler quality used by the device engine when bridging between
        // the hardware sample rate and the engine sample rate. Persisted so
        // the user's CPU/quality trade-off survives restarts.
        int resamplerQuality { 2 }; // 0=Linear 1=CatmullRom 2=Lagrange 3=WindowedSinc
        // Plugin Delay Compensation master switch. When enabled the graph
        // runtime inserts delay lines so parallel paths stay sample-aligned
        // with the longest-latency chain.
        bool pdcEnabled { true };
    };

    class Config
    {
    public:
        Config() = default;

        void setEngineSettings(const EngineSettings& settings) { engineSettings = settings; }
        EngineSettings getEngineSettings() const noexcept { return engineSettings; }

        void setPluginDirectories(const std::vector<juce::File>& dirs);
        const std::vector<juce::File>& getPluginDirectories() const noexcept { return pluginDirectories; }

        void setDefaultPreset(const juce::File& presetFile) { defaultPreset = presetFile; }
        juce::File getDefaultPreset() const noexcept { return defaultPreset; }
        void clearDefaultPreset() { defaultPreset = juce::File(); }

        void setLanguage(const juce::String& languageCode) { language = languageCode; }
        juce::String getLanguage() const noexcept { return language; }

        // The AudioDeviceManager state as an XML string. Persisting this lets
        // the chosen driver / input / output device, sample rate, buffer size
        // and active channel mask survive a restart, which the engine settings
        // alone do not capture. Empty when never saved.
        void setAudioDeviceState(const juce::String& state) { audioDeviceState = state; }
        juce::String getAudioDeviceState() const noexcept { return audioDeviceState; }

        bool load(const juce::File& file);
        bool save(const juce::File& file) const;

    private:
        EngineSettings engineSettings;
        std::vector<juce::File> pluginDirectories;
        juce::File defaultPreset;
        juce::String language { "en" };
        juce::String audioDeviceState;
    };
}
