#include "persist/Preset.h"

namespace host::persist
{
    bool Preset::load(const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        juce::FileInputStream stream(file);
        if (! stream.openedOk())
            return false;

        name = stream.readString();
        auto remaining = static_cast<size_t>(stream.getNumBytesRemaining());
        state.setSize(remaining);
        stream.read(state.getData(), static_cast<int>(remaining));
        return true;
    }

    bool Preset::save(const juce::File& file) const
    {
        juce::FileOutputStream stream(file);
        if (! stream.openedOk())
            return false;

        stream.writeString(name);
        stream.write(state.getData(), state.getSize());
        stream.flush();
        return true;
    }
}
