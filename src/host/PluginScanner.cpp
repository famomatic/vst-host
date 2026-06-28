#include "host/PluginScanner.h"

#include <vector>
#include <array>
#include <filesystem>

namespace host::plugin
{
    namespace
    {
        juce::File resolveVst3Module(const juce::File& entry)
        {
            if (! entry.isDirectory())
                return entry;

            const auto isCandidateVst3ModuleFile = [](const juce::File& candidate) -> bool
            {
                const auto ext = candidate.getFileExtension().toLowerCase();
#if JUCE_WINDOWS
                return ext == ".vst3";
#elif JUCE_MAC
                const bool isBundleBinary = ext.isEmpty()
                                            && candidate.getParentDirectory().getFileName().equalsIgnoreCase("MacOS");
                return ext == ".dylib" || isBundleBinary;
#else
                return ext == ".so";
#endif
            };

            const auto findModuleInDirectory = [&](const juce::File& directory) -> juce::File
            {
                if (! directory.isDirectory())
                    return {};

                juce::DirectoryIterator it(directory,
                                           true,
                                           "*",
                                           juce::File::findFiles);
                while (it.next())
                {
                    auto candidate = it.getFile();
                    if (candidate.isDirectory())
                        continue;

                    if (isCandidateVst3ModuleFile(candidate))
                        return candidate;
                }

                return {};
            };

            const juce::File contents = entry.getChildFile("Contents");
            const std::array<juce::String, 3> preferredSubdirs { juce::String("x86_64-win"),
                                                                 juce::String("x86_64-linux"),
                                                                 juce::String("MacOS") };

            if (contents.isDirectory())
            {
                for (const auto& name : preferredSubdirs)
                {
                    const auto preferred = contents.getChildFile(name);
                    const auto module = findModuleInDirectory(preferred);
                    if (module.existsAsFile())
                        return module;
                }

                const auto module = findModuleInDirectory(contents);
                if (module.existsAsFile())
                    return module;
            }

            const auto module = findModuleInDirectory(entry);
            if (module.existsAsFile())
                return module;

            return entry;
        }

        bool hasPluginExtension(const juce::File& file)
        {
            auto ext = file.getFileExtension().toLowerCase();
            return ext == ".vst3" || ext == ".dll";
        }

        PluginFormat inferFormatFromCandidate(const juce::File& candidate)
        {
            return candidate.hasFileExtension(".vst3") ? PluginFormat::VST3 : PluginFormat::VST2;
        }

        PluginInfo makeVst3Descriptor(const juce::File& sourceEntry, const juce::File& modulePath)
        {
            PluginInfo info;
            const auto chosenFile = modulePath.existsAsFile() ? modulePath : sourceEntry;
            info.path = std::filesystem::path(chosenFile.getFullPathName().toStdString());
            info.name = sourceEntry.getFileNameWithoutExtension().toStdString();
            if (info.name.empty())
                info.name = chosenFile.getFileNameWithoutExtension().toStdString();
            info.id = info.path.generic_string();
            info.format = PluginFormat::VST3;
            info.ins = 0;
            info.outs = 0;
            return info;
        }

        PluginInfo makeVst2Descriptor(const juce::File& sourceEntry, const juce::File& modulePath)
        {
            PluginInfo info;
            const auto chosenFile = modulePath.existsAsFile() ? modulePath : sourceEntry;
            info.path = std::filesystem::path(chosenFile.getFullPathName().toStdString());
            info.name = sourceEntry.getFileNameWithoutExtension().toStdString();
            if (info.name.empty())
                info.name = chosenFile.getFileNameWithoutExtension().toStdString();
            info.id = info.path.generic_string();
            info.format = PluginFormat::VST2;
            info.ins = 0;
            info.outs = 0;
            return info;
        }
    }

    PluginScanner::PluginScanner() = default;

    PluginScanner::~PluginScanner()
    {
        cancelScan();
    }

    void PluginScanner::addSearchPath(const juce::File& path)
    {
        if (path.getFullPathName().isEmpty())
            return;

        const juce::ScopedLock lock(stateLock);
        if (! searchPaths.contains(path))
            searchPaths.add(path);
    }

    void PluginScanner::removeSearchPath(const juce::File& path)
    {
        const juce::ScopedLock lock(stateLock);
        searchPaths.removeFirstMatchingValue(path);
    }

    void PluginScanner::setSearchPaths(const std::vector<juce::File>& paths)
    {
        const juce::ScopedLock lock(stateLock);
        searchPaths.clearQuick();
        for (const auto& path : paths)
        {
            if (path.getFullPathName().isEmpty())
                continue;

            if (! searchPaths.contains(path))
                searchPaths.add(path);
        }
    }

    void PluginScanner::scanAsync()
    {
        if (scanning.exchange(true))
            return;

        cancelRequested.store(false);
        joinWorkerIfRunning();

        std::lock_guard<std::mutex> guard(workerMutex);
        workerThread = std::thread([this]
        {
            runScan();
            scanning.store(false);
            sendChangeMessage();
        });
    }

    void PluginScanner::cancelScan()
    {
        cancelRequested.store(true);
        scanning.store(false);
        joinWorkerIfRunning();
    }

    std::vector<PluginInfo> PluginScanner::getDiscoveredPlugins() const
    {
        const juce::ScopedLock lock(stateLock);
        return discovered;
    }

    bool PluginScanner::loadCache(const juce::File& cacheFile)
    {
        cacheLocation = cacheFile;
        if (! cacheFile.existsAsFile())
            return false;

        juce::String jsonText = cacheFile.loadFileAsString();
        juce::var value;
        auto result = juce::JSON::parse(jsonText, value);
        if (result.failed())
        {
            lastError = result.getErrorMessage();
            return false;
        }

        std::vector<PluginInfo> loaded;

        if (auto* object = value.getDynamicObject())
        {
            auto list = object->getProperty("plugins");
            if (auto* arr = list.getArray())
            {
                for (auto& pluginVar : *arr)
                {
                    if (auto* obj = pluginVar.getDynamicObject())
                    {
                        PluginInfo info;
                        info.id = obj->getProperty("id").toString().toStdString();
                        info.name = obj->getProperty("name").toString().toStdString();
                        info.format = obj->getProperty("format").toString() == "VST2" ? PluginFormat::VST2 : PluginFormat::VST3;
                        info.path = obj->getProperty("path").toString().toStdString();
                        if (auto ins = obj->getProperty("ins"); ! ins.isVoid())
                            info.ins = static_cast<int>(ins);
                        if (auto outs = obj->getProperty("outs"); ! outs.isVoid())
                            info.outs = static_cast<int>(outs);
                        info.latency = static_cast<int>(obj->getProperty("latency"));

                        if (! info.id.empty() || ! info.name.empty() || ! info.path.empty())
                            loaded.push_back(std::move(info));
                    }
                }
            }
        }

        {
            const juce::ScopedLock lock(stateLock);
            discovered = std::move(loaded);
        }

        sendChangeMessage();
        return true;
    }

    bool PluginScanner::saveCache(const juce::File& cacheFile) const
    {
        if (! cacheFile.getParentDirectory().isDirectory())
            cacheFile.getParentDirectory().createDirectory();

        juce::DynamicObject::Ptr root(new juce::DynamicObject());
        root->setProperty("v", 1);
        root->setProperty("scannedAt", juce::Time::getCurrentTime().toISO8601(true));

        juce::Array<juce::var> pluginArray;
        std::vector<PluginInfo> snapshot;
        {
            const juce::ScopedLock lock(stateLock);
            snapshot = discovered;
        }

        for (const auto& info : snapshot)
        {
            juce::DynamicObject::Ptr obj(new juce::DynamicObject());
            obj->setProperty("id", juce::String(info.id));
            obj->setProperty("name", juce::String(info.name));
            obj->setProperty("format", info.format == PluginFormat::VST3 ? juce::String("VST3") : juce::String("VST2"));
            obj->setProperty("path", juce::String(info.path.generic_string()));
            obj->setProperty("ins", info.ins);
            obj->setProperty("outs", info.outs);
            obj->setProperty("latency", info.latency);
            obj->setProperty("blacklisted", false);
            juce::var pluginVar(obj.get());
            pluginArray.add(pluginVar);
        }

        root->setProperty("plugins", juce::var(pluginArray));
        auto jsonString = juce::JSON::toString(juce::var(root.get()), true);
        return cacheFile.replaceWithText(jsonString);
    }

    void PluginScanner::runScan()
    {
        std::vector<PluginInfo> results;

        juce::Array<juce::File> pathsCopy;
        {
            const juce::ScopedLock lock(stateLock);
            pathsCopy = searchPaths;
        }

        for (auto& path : pathsCopy)
        {
            if (cancelRequested.load())
                break;

            std::vector<juce::File> pending;
            pending.push_back(path);

            while (! pending.empty())
            {
                if (cancelRequested.load())
                    break;

                auto current = pending.back();
                pending.pop_back();

                if (! current.exists())
                    continue;

                if (! current.isDirectory())
                {
                    if (! hasPluginExtension(current))
                        continue;

                    juce::File modulePath = current.hasFileExtension(".vst3") ? resolveVst3Module(current) : current;
                    if (inferFormatFromCandidate(current) == PluginFormat::VST3)
                        results.push_back(makeVst3Descriptor(current, modulePath));
                    else
                        results.push_back(makeVst2Descriptor(current, modulePath));
                    continue;
                }

                juce::DirectoryIterator it(current,
                                           false,
                                           "*",
                                           juce::File::findFilesAndDirectories);
                while (it.next())
                {
                    if (cancelRequested.load())
                        break;

                    auto candidate = it.getFile();
                    if (candidate.isDirectory())
                    {
                        if (hasPluginExtension(candidate))
                        {
                            juce::File modulePath = resolveVst3Module(candidate);
                            if (inferFormatFromCandidate(candidate) == PluginFormat::VST3)
                                results.push_back(makeVst3Descriptor(candidate, modulePath));
                            else
                                results.push_back(makeVst2Descriptor(candidate, modulePath));
                        }
                        else
                        {
                            pending.push_back(candidate);
                        }
                        continue;
                    }

                    if (! hasPluginExtension(candidate))
                        continue;

                    juce::File modulePath = candidate.hasFileExtension(".vst3") ? resolveVst3Module(candidate) : candidate;

                    if (inferFormatFromCandidate(candidate) == PluginFormat::VST3)
                        results.push_back(makeVst3Descriptor(candidate, modulePath));
                    else
                        results.push_back(makeVst2Descriptor(candidate, modulePath));
                }
            }
        }

        if (cancelRequested.load())
            return;

        {
            const juce::ScopedLock lock(stateLock);
            discovered = std::move(results);
        }

        if (cacheLocation.getFullPathName().isNotEmpty())
            saveCache(cacheLocation);
    }

    void PluginScanner::joinWorkerIfRunning()
    {
        std::thread threadToJoin;
        {
            std::lock_guard<std::mutex> guard(workerMutex);
            if (! workerThread.joinable())
                return;

            threadToJoin = std::move(workerThread);
        }

        if (threadToJoin.joinable() && threadToJoin.get_id() != std::this_thread::get_id())
            threadToJoin.join();
    }
}
