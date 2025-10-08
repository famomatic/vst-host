#include "host/PluginScanner.h"

#include <thread>
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

            const auto findModuleInDirectory = [](const juce::File& directory) -> juce::File
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

                    auto ext = candidate.getFileExtension().toLowerCase();
                    const bool isBundleBinary = ext.isEmpty()
                                                && candidate.getParentDirectory().getFileName().equalsIgnoreCase("MacOS");

                    if (ext == ".vst3" || ext == ".dll" || ext == ".so" || ext == ".dylib" || isBundleBinary)
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
    }

    PluginScanner::PluginScanner() = default;

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

        std::thread([this]
        {
            runScan();
            scanning.store(false);
            sendChangeMessage();
        }).detach();
    }

    void PluginScanner::cancelScan()
    {
        scanning.store(false);
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
                    PluginInfo info;
                    if (auto* obj = pluginVar.getDynamicObject())
                    {
                        info.id = obj->getProperty("id").toString().toStdString();
                        info.name = obj->getProperty("name").toString().toStdString();
                        info.format = obj->getProperty("format").toString() == "VST2" ? PluginFormat::VST2 : PluginFormat::VST3;
                        info.path = obj->getProperty("path").toString().toStdString();
                        if (auto ins = obj->getProperty("ins"); ! ins.isVoid())
                            info.ins = static_cast<int>(ins);
                        if (auto outs = obj->getProperty("outs"); ! outs.isVoid())
                            info.outs = static_cast<int>(outs);
                        info.latency = static_cast<int>(obj->getProperty("latency"));
                    }
                    loaded.push_back(info);
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
            std::vector<juce::File> pending;
            pending.push_back(path);

            while (! pending.empty())
            {
                auto current = pending.back();
                pending.pop_back();

                if (! current.exists())
                    continue;

                if (! current.isDirectory())
                {
                    if (! hasPluginExtension(current))
                        continue;

                    juce::File modulePath = current.hasFileExtension(".vst3") ? resolveVst3Module(current) : current;

                    PluginInfo info;
                    info.path = modulePath.existsAsFile()
                                    ? std::filesystem::path(modulePath.getFullPathName().toStdString())
                                    : std::filesystem::path(current.getFullPathName().toStdString());
                    info.name = current.getFileNameWithoutExtension().toStdString();
                    info.id = modulePath.existsAsFile()
                                  ? modulePath.getFullPathName().toStdString()
                                  : current.getFullPathName().toStdString();
                    info.format = current.hasFileExtension(".dll") ? PluginFormat::VST2 : PluginFormat::VST3;
                    info.ins = 2;
                    info.outs = 2;
                    results.push_back(std::move(info));
                    continue;
                }

                juce::DirectoryIterator it(current,
                                           false,
                                           "*",
                                           juce::File::findFilesAndDirectories);
                while (it.next())
                {
                    auto candidate = it.getFile();
                    if (candidate.isDirectory())
                    {
                        if (hasPluginExtension(candidate))
                        {
                            juce::File modulePath = resolveVst3Module(candidate);

                            PluginInfo info;
                            info.path = modulePath.existsAsFile()
                                            ? std::filesystem::path(modulePath.getFullPathName().toStdString())
                                            : std::filesystem::path(candidate.getFullPathName().toStdString());
                            info.name = candidate.getFileNameWithoutExtension().toStdString();
                            info.id = modulePath.existsAsFile()
                                          ? modulePath.getFullPathName().toStdString()
                                          : candidate.getFullPathName().toStdString();
                            info.format = PluginFormat::VST3;
                            info.ins = 2;
                            info.outs = 2;
                            results.push_back(std::move(info));
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

                    PluginInfo info;
                    info.path = modulePath.existsAsFile()
                                    ? std::filesystem::path(modulePath.getFullPathName().toStdString())
                                    : std::filesystem::path(candidate.getFullPathName().toStdString());
                    info.name = candidate.getFileNameWithoutExtension().toStdString();
                    info.id = modulePath.existsAsFile()
                                  ? modulePath.getFullPathName().toStdString()
                                  : candidate.getFullPathName().toStdString();
                    info.format = candidate.hasFileExtension(".dll") ? PluginFormat::VST2 : PluginFormat::VST3;
                    info.ins = 2;
                    info.outs = 2;
                    results.push_back(std::move(info));
                }
            }
        }

        {
            const juce::ScopedLock lock(stateLock);
            discovered = std::move(results);
        }

        if (cacheLocation.getFullPathName().isNotEmpty())
            saveCache(cacheLocation);
    }
}
