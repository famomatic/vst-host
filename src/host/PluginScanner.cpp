#include "host/PluginScanner.h"

#include <thread>
#include <vector>

namespace host::plugin
{
    namespace
    {
        bool hasPluginExtension(const juce::File& file)
        {
            auto ext = file.getFileExtension().toLowerCase();
            return ext == ".vst3" || ext == ".dll";
        }
    }

    PluginScanner::PluginScanner() = default;

    void PluginScanner::addSearchPath(const juce::File& path)
    {
        if (! path.exists())
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
                        info.identifier = obj->getProperty("id").toString().toStdString();
                        info.name = obj->getProperty("name").toString().toStdString();
                        info.format = obj->getProperty("format").toString() == "VST2" ? PluginFormat::VST2 : PluginFormat::VST3;
                        info.path = obj->getProperty("path").toString().toStdString();
                        info.numInputs = static_cast<int>(obj->getProperty("ins"));
                        info.numOutputs = static_cast<int>(obj->getProperty("outs"));
                        info.latency = static_cast<int>(obj->getProperty("latency"));
                        if (auto category = obj->getProperty("category").toString(); category.isNotEmpty())
                            info.categories = { category.toStdString() };
                    }
                    loaded.push_back(info);
                }
            }
        }

        {
            const juce::ScopedLock lock(stateLock);
            discovered = std::move(loaded);
        }

        return true;
    }

    bool PluginScanner::saveCache(const juce::File& cacheFile) const
    {
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
            obj->setProperty("id", info.identifier);
            obj->setProperty("name", info.name);
            obj->setProperty("format", info.format == PluginFormat::VST3 ? "VST3" : "VST2");
            obj->setProperty("path", info.path);
            obj->setProperty("ins", info.numInputs);
            obj->setProperty("outs", info.numOutputs);
            obj->setProperty("latency", info.latency);
            obj->setProperty("category", info.categories.empty() ? juce::String() : juce::String(info.categories.front()));
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
            juce::DirectoryIterator it(path, true, "*", juce::File::findFiles);
            while (it.next())
            {
                auto file = it.getFile();
                if (! hasPluginExtension(file))
                    continue;

                PluginInfo info;
                info.path = file.getFullPathName().toStdString();
                info.name = file.getFileNameWithoutExtension().toStdString();
                info.identifier = juce::Uuid().toString().toStdString();
                info.format = file.hasFileExtension(".dll") ? PluginFormat::VST2 : PluginFormat::VST3;
                info.numInputs = 2;
                info.numOutputs = 2;
                info.categories = { "Effect" };
                results.push_back(std::move(info));
            }
        }

        {
            const juce::ScopedLock lock(stateLock);
            discovered = std::move(results);
        }

        if (cacheLocation.existsAsFile())
            saveCache(cacheLocation);
    }
}
