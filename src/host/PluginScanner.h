#pragma once

#include "host/PluginHost.h"

#include <juce_core/juce_core.h>
#include <atomic>

namespace host::plugin
{
    class PluginScanner : public juce::ChangeBroadcaster
    {
    public:
        PluginScanner();

        void addSearchPath(const juce::File& path);
        void removeSearchPath(const juce::File& path);
        const juce::Array<juce::File>& getSearchPaths() const noexcept { return searchPaths; }

        void scanAsync();
        void cancelScan();

        std::vector<PluginInfo> getDiscoveredPlugins() const;

        bool loadCache(const juce::File& cacheFile);
        bool saveCache(const juce::File& cacheFile) const;

    private:
        void runScan();

        juce::Array<juce::File> searchPaths;
        std::vector<PluginInfo> discovered;
        juce::CriticalSection stateLock;
        juce::String lastError;
        juce::File cacheLocation;
        std::atomic<bool> scanning { false };
    };
}
