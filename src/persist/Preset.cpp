#include "persist/Preset.h"

namespace host::persist
{
namespace
{
    // Magic header that distinguishes the JSON preset format from any raw
    // binary blob written by older versions of this class.
    constexpr const char* kJsonMagic = "{";
}

    bool Preset::load(const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        const auto text = file.loadFileAsString();
        if (text.trimStart().startsWith(kJsonMagic))
        {
            juce::var root;
            const auto result = juce::JSON::parse(text, root);
            if (result.failed())
                return false;

            if (auto* object = root.getDynamicObject())
            {
                name = object->getProperty("name").toString();
                if (name.isEmpty())
                    name = file.getFileNameWithoutExtension();

                const auto stateText = object->getProperty("state").toString();
                if (stateText.isNotEmpty())
                    state.fromBase64Encoding(stateText);
                else
                    state.reset();

                return true;
            }

            return false;
        }

        // Legacy raw format: first line is the name, remaining bytes are state.
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
        if (! file.getParentDirectory().isDirectory())
            file.getParentDirectory().createDirectory();

        juce::DynamicObject::Ptr root(new juce::DynamicObject());
        root->setProperty("name", name);
        root->setProperty("state", state.toBase64Encoding());

        const auto json = juce::JSON::toString(juce::var(root.get()), true);
        return file.replaceWithText(json);
    }

    bool Preset::captureFromState(const juce::MemoryBlock& newState)
    {
        if (newState.getSize() == 0)
            return false;

        state = newState;
        return true;
    }

    bool Preset::applyToState(juce::MemoryBlock& outState) const
    {
        if (state.getSize() == 0)
            return false;

        outState = state;
        return true;
    }
} // namespace host::persist
