#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

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
        int latency = 0; // samples
    };

    class PluginInstance
    {
    public:
        virtual ~PluginInstance() = default;
        virtual void prepare(double sr, int block) = 0;
        virtual void process(float** in, int inCh, float** out, int outCh, int numFrames) = 0;
        [[nodiscard]] virtual int latencySamples() const = 0;
        virtual bool getState(std::vector<std::uint8_t>& out) = 0;
        virtual bool setState(const std::uint8_t* data, std::size_t len) = 0;
        virtual bool queryRuntimeInfo(PluginInfo& ioInfo) const { juce::ignoreUnused(ioInfo); return false; }
        [[nodiscard]] virtual bool hasEditor() const { return false; }
        // Reports whether the hosted editor supports live resizing. Hosts use this
        // to decide if the editor dialog should be user-resizable and to size it.
        [[nodiscard]] virtual bool isEditorResizable() const { return false; }
        virtual std::unique_ptr<juce::Component> createEditorComponent() { return {}; }
    };

    // Owns the JUCE AudioPluginFormatManager (VST2 + VST3 hosting). All plugin
    // loading goes through this so JUCE owns the full editor/sizing/state
    // lifecycle - the same model the reference host (LightHost) uses. Replaces
    // the previous hand-rolled VST2 (Xaymar SDK) and VST3 (raw IPlugView)
    // loaders, which were the root cause of the editor sizing/open bugs
    // (clipping on open, shrinking on focus loss, Fresh Air not opening).
    class PluginLoader
    {
    public:
        PluginLoader();

        // Loads a plugin (VST2 or VST3) by PluginInfo. Returns null on failure.
        std::unique_ptr<PluginInstance> load(const PluginInfo& info, juce::String* error = nullptr);

        // Looks up a PluginDescription matching the given path/identifier, used
        // when restoring projects from saved data and by the scanner to cache.
        bool describePlugin(const PluginInfo& info, juce::PluginDescription& outDesc) const;

        juce::AudioPluginFormatManager& formatManager() { return formatManager_; }
        juce::KnownPluginList& knownList() { return knownPluginList; }

    private:
        juce::AudioPluginFormatManager formatManager_;
        juce::KnownPluginList knownPluginList;
    };
} // namespace host::plugin
