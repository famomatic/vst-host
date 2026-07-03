#include "host/PluginHost.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace host::plugin
{
namespace
{
    [[nodiscard]] juce::String pathToString(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        const auto asUtf8 = path.generic_u8string();
        return juce::String::fromUTF8(reinterpret_cast<const char*>(asUtf8.data()),
                                      static_cast<int>(asUtf8.size()));
    }

    // Wraps a juce::AudioPluginInstance behind the host::plugin::PluginInstance
    // interface, so the graph nodes, persistence and GUI keep working without
    // knowing that JUCE now owns the plugin lifecycle. The editor component is
    // created via AudioProcessor::createEditorIfNeeded() - exactly what LightHost
    // does - so JUCE handles all the IPlugView / effEditOpen sizing internally.
    class JucePluginInstance final : public PluginInstance
    {
    public:
        explicit JucePluginInstance(std::unique_ptr<juce::AudioPluginInstance> instanceIn,
                                    const PluginInfo& info)
            : instance(std::move(instanceIn))
        {
            if (instance != nullptr)
            {
                juce::PluginDescription desc;
                instance->fillInPluginDescription(desc);
                storedInfo = info;
                if (storedInfo.id.empty())
                    storedInfo.id = desc.fileOrIdentifier.toStdString();
                if (storedInfo.name.empty())
                    storedInfo.name = desc.name.toStdString();
            }
        }

        void prepare(double sr, int block) override
        {
            if (! instance)
                return;

            instance->setRateAndBufferSizeDetails(sr, block);
            instance->prepareToPlay(sr, block);

            prepared = true;
            const int inCh = instance->getTotalNumInputChannels();
            const int outCh = instance->getTotalNumOutputChannels();
            processBuffer.setSize(juce::jmax(inCh, outCh, 1), block);
        }

        void process(float** in, int inCh, float** out, int outCh, int numFrames) override
        {
            if (! instance || ! prepared)
                return;

            // JUCE's processBlock owns the buffer; copy inputs in, run, then
            // copy outputs back out. The buffer is sized to max(in,out) so a
            // mono->stereo plugin (or vice versa) does not overflow.
            const int maxCh = juce::jmax(inCh, outCh, 1);
            if (processBuffer.getNumChannels() < maxCh || processBuffer.getNumSamples() < numFrames)
                processBuffer.setSize(maxCh, juce::jmax(numFrames, processBuffer.getNumSamples()));

            for (int c = 0; c < inCh && c < maxCh; ++c)
            {
                if (in[c] != nullptr)
                    std::memcpy(processBuffer.getWritePointer(c), in[c],
                                static_cast<std::size_t>(numFrames) * sizeof(float));
                else
                    processBuffer.clear(c, 0, numFrames);
            }
            for (int c = inCh; c < maxCh; ++c)
                processBuffer.clear(c, 0, numFrames);

            juce::MidiBuffer midi;
            instance->processBlock(processBuffer, midi);

            for (int c = 0; c < outCh && c < maxCh; ++c)
            {
                if (out[c] != nullptr)
                    std::memcpy(out[c], processBuffer.getReadPointer(c),
                                static_cast<std::size_t>(numFrames) * sizeof(float));
            }
        }

        [[nodiscard]] int latencySamples() const override
        {
            return instance ? instance->getLatencySamples() : 0;
        }

        bool getState(std::vector<std::uint8_t>& out) override
        {
            if (! instance)
                return false;

            juce::MemoryBlock block;
            instance->getStateInformation(block);
            if (block.getSize() == 0)
                return false;

            out.resize(block.getSize());
            std::memcpy(out.data(), block.getData(), block.getSize());
            return true;
        }

        bool setState(const std::uint8_t* data, std::size_t len) override
        {
            if (! instance || ! data || len == 0)
                return false;

            instance->setStateInformation(data, static_cast<int>(len));
            return true;
        }

        bool queryRuntimeInfo(PluginInfo& ioInfo) const override
        {
            if (! instance)
                return false;

            ioInfo.ins = std::max(0, instance->getTotalNumInputChannels());
            ioInfo.outs = std::max(0, instance->getTotalNumOutputChannels());
            ioInfo.latency = std::max(0, instance->getLatencySamples());
            return true;
        }

        [[nodiscard]] bool hasEditor() const override
        {
            return instance != nullptr && instance->hasEditor();
        }

        [[nodiscard]] bool isEditorResizable() const override
        {
            // JUCE editors report their own resizability; default to allowing
            // resize so the user can grow non-fixed editors. The wrapping
            // DocumentWindow honors the editor's constrainer either way.
            return true;
        }

        std::unique_ptr<juce::Component> createEditorComponent() override
        {
            if (! instance)
                return {};

            // createEditorIfNeeded returns an AudioProcessorEditor owned by the
            // caller. Wrap it so the GUI receives a plain juce::Component that
            // reports the editor's preferred size - JUCE handles all the native
            // view attach/sizing that previously broke Fresh Air / LUveler.
            std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
            if (! editor)
                return {};

            return std::unique_ptr<juce::Component>(editor.release());
        }

    private:
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::AudioBuffer<float> processBuffer;
        PluginInfo storedInfo;
        bool prepared { false };
    };
} // namespace

PluginLoader::PluginLoader()
{
    formatManager_.addDefaultFormats();
}

bool PluginLoader::describePlugin(const PluginInfo& info, juce::PluginDescription& outDesc) const
{
    if (info.path.empty())
        return false;

    const auto formats = formatManager_.getFormats();
    const auto formatName = (info.format == PluginFormat::VST2) ? "VST" : "VST3";

    for (auto* fmt : formats)
    {
        if (! fmt->getName().equalsIgnoreCase(formatName))
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        fmt->findAllTypesForFile(found, pathToString(info.path));
        if (found.isEmpty())
            continue;

        // Prefer a match by saved id/name; otherwise take the first result.
        const juce::String idStr = juce::String(info.id);
        const juce::String nameStr = juce::String(info.name);
        juce::PluginDescription* chosen = nullptr;
        for (auto* d : found)
        {
            if ((! idStr.isEmpty() && d->fileOrIdentifier == idStr)
                || (! nameStr.isEmpty() && d->descriptiveName == nameStr))
            {
                chosen = d;
                break;
            }
        }
        if (chosen == nullptr)
            chosen = found.getFirst();

        outDesc = *chosen;
        return true;
    }

    return false;
}

std::unique_ptr<PluginInstance> PluginLoader::load(const PluginInfo& info, juce::String* error)
{
    juce::PluginDescription desc;
    if (! describePlugin(info, desc))
    {
        // Fallback: build a description directly from the path so a plugin that
        // was never scanned (e.g. restored from a project on a fresh machine)
        // can still be loaded by file.
        desc.pluginFormatName = (info.format == PluginFormat::VST2) ? "VST" : "VST3";
        desc.fileOrIdentifier = pathToString(info.path);
        const auto pathStr = pathToString(info.path);
        desc.descriptiveName = juce::File(pathStr).getFileNameWithoutExtension();
        if (! info.name.empty())
            desc.descriptiveName = juce::String(info.name);
    }

    juce::String localError;
    auto instance = formatManager_.createPluginInstance(desc,
                                                        44100.0,
                                                        512,
                                                        localError);
    if (! instance)
    {
        juce::String message("Plugin load failed [");
        message += (info.format == PluginFormat::VST3 ? "VST3" : "VST2");
        if (! info.name.empty())
            message += " " + juce::String(info.name);
        if (! info.path.empty())
            message += " (" + pathToString(info.path) + ")";
        message += "]: " + localError;
        juce::Logger::writeToLog(message.trimEnd());

        if (error != nullptr)
            *error = localError;
        return {};
    }

    return std::make_unique<JucePluginInstance>(std::move(instance), info);
}
} // namespace host::plugin
