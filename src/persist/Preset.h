#pragma once

#include <juce_core/juce_core.h>

namespace host::persist
{
    class Preset
    {
    public:
        bool load(const juce::File& file);
        bool save(const juce::File& file) const;

        // Convenience helpers that bridge a Preset to a plugin instance's
        // getState/setState contract. Returning false indicates the instance
        // did not provide any state.
        bool captureFromState(const juce::MemoryBlock& state);
        bool applyToState(juce::MemoryBlock& state) const;

        juce::String getName() const noexcept { return name; }
        void setName(juce::String newName) { name = std::move(newName); }
        const juce::MemoryBlock& getState() const noexcept { return state; }
        void setState(juce::MemoryBlock newState) { state = std::move(newState); }

    private:
        juce::String name { "Default" };
        juce::MemoryBlock state;
    };
}
