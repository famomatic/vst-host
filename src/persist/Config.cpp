#include "persist/Config.h"

namespace host::persist
{
    void Config::setPluginDirectories(const std::vector<juce::File>& dirs)
    {
        pluginDirectories = dirs;
    }

    bool Config::load(const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        juce::String jsonText = file.loadFileAsString();
        juce::var root;
        auto result = juce::JSON::parse(jsonText, root);
        if (result.failed())
            return false;

        if (auto* object = root.getDynamicObject())
        {
            engineSettings.sampleRate = object->getProperty("sampleRate");
            engineSettings.blockSize = static_cast<int>(object->getProperty("blockSize"));
            engineSettings.resamplerQuality = static_cast<int>(object->getProperty("resamplerQuality"));
            // Legacy files omit PDC/resampler fields; fall back to defaults so
            // old configs still load cleanly.
            if (engineSettings.resamplerQuality <= 0)
                engineSettings.resamplerQuality = 2;
            engineSettings.pdcEnabled = static_cast<bool>(object->getProperty("pdcEnabled"));

            pluginDirectories.clear();
            if (auto* arr = object->getProperty("pluginDirectories").getArray())
            {
                for (auto& entry : *arr)
                    pluginDirectories.emplace_back(entry.toString());
            }

            if (auto presetValue = object->getProperty("defaultPreset"); presetValue.isString())
                defaultPreset = juce::File(presetValue.toString());
            else
                defaultPreset = juce::File();

            if (auto languageValue = object->getProperty("language"); languageValue.isString())
                language = languageValue.toString();

            if (auto stateValue = object->getProperty("audioDeviceState"); stateValue.isString())
                audioDeviceState = stateValue.toString();
        }

        return true;
    }

    bool Config::save(const juce::File& file) const
    {
        juce::DynamicObject::Ptr obj(new juce::DynamicObject());
        obj->setProperty("sampleRate", engineSettings.sampleRate);
        obj->setProperty("blockSize", engineSettings.blockSize);
        obj->setProperty("resamplerQuality", engineSettings.resamplerQuality);
        obj->setProperty("pdcEnabled", engineSettings.pdcEnabled);

        juce::Array<juce::var> directories;
        for (auto& dir : pluginDirectories)
            directories.add(dir.getFullPathName());

        obj->setProperty("pluginDirectories", juce::var(directories));
        obj->setProperty("defaultPreset", defaultPreset.getFullPathName());
        obj->setProperty("language", language);
        obj->setProperty("audioDeviceState", audioDeviceState);
        auto json = juce::JSON::toString(juce::var(obj.get()), true);
        return file.replaceWithText(json);
    }
}
