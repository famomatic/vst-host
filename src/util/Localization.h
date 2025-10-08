#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <unordered_map>
#include <vector>

namespace host::i18n
{
    class LocalizationManager : public juce::ChangeBroadcaster
    {
    public:
        static LocalizationManager& getInstance();

        void registerLanguage(const juce::String& code,
                              const juce::String& displayName,
                              const juce::StringPairArray& strings);

        bool loadOverridesFromFile(const juce::File& file);

        bool setLanguage(const juce::String& code);
        [[nodiscard]] juce::String getLanguage() const noexcept { return juce::String(currentCode); }

        [[nodiscard]] juce::String translate(const juce::String& key) const;

        [[nodiscard]] std::vector<std::pair<juce::String, juce::String>> getAvailableLanguages() const;

    private:
        LocalizationManager();

        [[nodiscard]] const juce::StringPairArray& getTable(const std::string& code) const;

        std::unordered_map<std::string, juce::StringPairArray> tables;
        std::unordered_map<std::string, juce::String> names;
        std::vector<std::string> orderedCodes;
        std::string currentCode { "en" };
        juce::StringPairArray emptyTable;
    };

    inline LocalizationManager& manager() { return LocalizationManager::getInstance(); }
    juce::String tr(const juce::String& key);
}
