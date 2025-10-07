#pragma once

#include <juce_core/juce_core.h>

namespace host::persist
{
    class Preset
    {
    public:
        bool load(const juce::File& file);
        bool save(const juce::File& file) const;

        juce::String getName() const noexcept { return name; }
        void setName(juce::String newName) { name = std::move(newName); }

    private:
        juce::String name { "Default" };
        juce::MemoryBlock state;
    };
}
