#pragma once

#include "host/PluginHost.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace host::plugin
{
    class PluginScanner : public juce::ChangeBroadcaster
    {
    public:
        PluginScanner();
        ~PluginScanner() override;

        void addSearchPath(const juce::File& path);
        void removeSearchPath(const juce::File& path);
        const juce::Array<juce::File>& getSearchPaths() const noexcept { return searchPaths; }
        void setSearchPaths(const std::vector<juce::File>& paths);

        void scanAsync();
        void cancelScan();

        std::vector<PluginInfo> getDiscoveredPlugins() const;

        bool loadCache(const juce::File& cacheFile);
        bool saveCache(const juce::File& cacheFile) const;

    private:
        void runScan();
        void joinWorkerIfRunning();

        juce::Array<juce::File> searchPaths;
        std::vector<PluginInfo> discovered;
        juce::CriticalSection stateLock;
        std::mutex workerMutex;
        juce::String lastError;
        juce::File cacheLocation;
        std::atomic<bool> scanning { false };
        std::atomic<bool> cancelRequested { false };
        std::thread workerThread;
    };
}
