#include "host/PluginHost.h"
#include "host/SharedLibrary.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <vector>
#include <filesystem>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#ifndef VST_HOST_OPCODE_GET_TIME
#define VST_HOST_OPCODE_GET_TIME 0x07
#endif

#if defined(VST_VERSION)
 #undef VST_VERSION
#endif
#if defined(kVstVersionMajor)
 #undef kVstVersionMajor
#endif
#if defined(kVstVersionMinor)
 #undef kVstVersionMinor
#endif
#if defined(kVstVersionSub)
 #undef kVstVersionSub
#endif

#include <vst.h>

namespace host::plugin
{
namespace
{
    struct HostTimeInfo
    {
        double samplePos { 0.0 };
        double sampleRate { 0.0 };
        double nanoSeconds { 0.0 };
        double ppqPos { 0.0 };
        double tempo { 120.0 };
        double barStartPos { 0.0 };
        double cycleStartPos { 0.0 };
        double cycleEndPos { 0.0 };
        int32_t timeSigNumerator { 4 };
        int32_t timeSigDenominator { 4 };
        int32_t smpteOffset { 0 };
        int32_t smpteFrameRate { 0 };
        int32_t samplesToNextClock { 0 };
        int32_t flags { 0 };
    };

    constexpr int32_t kTimeInfoFlagTempoValid = 1 << 10;
    constexpr int32_t kTimeInfoFlagTimeSigValid = 1 << 13;
    constexpr int kVst2MaxChannels = 32;

    [[nodiscard]] juce::String pathToString(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        const auto asUtf8 = path.generic_u8string();
        return juce::String::fromUTF8(reinterpret_cast<const char*>(asUtf8.data()),
                                      static_cast<int>(asUtf8.size()));
    }

    void logPluginLoadFailure(const PluginInfo& info, const juce::String& reason)
    {
        juce::String message("Plugin load failed");
        message += " [" + juce::String(info.format == PluginFormat::VST3 ? "VST3" : "VST2") + "]";

        if (! info.name.empty())
            message += " " + juce::String(info.name);

        if (! info.path.empty())
            message += " (" + pathToString(info.path) + ")";

        message += ": " + reason;
        juce::Logger::writeToLog(message.trimEnd());
    }

    class Vst2EditorComponent;

    class Vst2PluginInstance final : public PluginInstance
    {
    public:
        Vst2PluginInstance(SharedLibrary&& moduleIn, vst_effect_t* effectIn)
            : module(std::move(moduleIn))
            , effect(effectIn)
        {
            if (effect != nullptr)
                effect->host_internal = this;
        }

        ~Vst2PluginInstance() override { shutdown(); }

        void prepare(double sr, int block) override
        {
            if (! effect)
                return;

            sampleRate = sr;
            blockSize = block;

            effect->control(effect, VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, nullptr, static_cast<float>(sr));
            effect->control(effect, VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, block, nullptr, 0.0f);
            effect->control(effect, VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 1, nullptr, 0.0f);
            active = true;
            replacing = (effect->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0 && effect->process_float != nullptr;
            supportsDoublePrecision = (effect->flags & VST_EFFECT_FLAG_SUPPORTS_DOUBLE) != 0 && effect->process_double != nullptr;

            scratch.resize(static_cast<std::size_t>(std::max(1, effect->num_outputs) * block));
            doubleInputScratch.clear();
            doubleOutputScratch.clear();
            latency = effect->delay;
        }

        void process(float** in, int inCh, float** out, int outCh, int numFrames) override
        {
            if (! effect || ! active || ! in || ! out)
                return;

            if (numFrames > blockSize)
                return;

            if (effect->num_inputs > inCh || effect->num_outputs > outCh)
                return;

            std::array<float*, kVst2MaxChannels> inputs {};
            std::array<float*, kVst2MaxChannels> outputs {};
            for (int c = 0; c < kVst2MaxChannels; ++c)
            {
                inputs[c] = (c < inCh && in[c] != nullptr) ? in[c] : nullptr;
                outputs[c] = (c < outCh && out[c] != nullptr) ? out[c] : nullptr;
            }

            if (supportsDoublePrecision && effect->process_double)
            {
                const std::size_t frameCapacity = static_cast<std::size_t>(std::max(blockSize, numFrames));
                const std::size_t inRequired = static_cast<std::size_t>(std::max(1, inCh)) * frameCapacity;
                const std::size_t outRequired = static_cast<std::size_t>(std::max(1, outCh)) * frameCapacity;

                if (doubleInputScratch.size() < inRequired)
                    doubleInputScratch.resize(inRequired);
                if (doubleOutputScratch.size() < outRequired)
                    doubleOutputScratch.resize(outRequired);

                std::array<const double*, kVst2MaxChannels> doubleInputs {};
                for (int c = 0; c < inCh; ++c)
                {
                    auto* dest = doubleInputScratch.data() + static_cast<std::size_t>(c) * frameCapacity;
                    doubleInputs[static_cast<std::size_t>(c)] = dest;

                    if (inputs[c] != nullptr)
                    {
                        const float* source = inputs[c];
                        for (int i = 0; i < numFrames; ++i)
                            dest[i] = static_cast<double>(source[i]);

                        if (frameCapacity > static_cast<std::size_t>(numFrames))
                            std::fill(dest + numFrames, dest + frameCapacity, 0.0);
                    }
                    else
                    {
                        std::fill(dest, dest + frameCapacity, 0.0);
                    }
                }

                std::array<double*, kVst2MaxChannels> doubleOutputs {};
                for (int c = 0; c < outCh; ++c)
                {
                    auto* dest = doubleOutputScratch.data() + static_cast<std::size_t>(c) * frameCapacity;
                    doubleOutputs[static_cast<std::size_t>(c)] = dest;
                    std::fill(dest, dest + frameCapacity, 0.0);
                }

                effect->process_double(effect, doubleInputs.data(), doubleOutputs.data(), numFrames);

                for (int c = 0; c < outCh; ++c)
                {
                    float* dest = outputs[c];
                    const double* src = doubleOutputs[static_cast<std::size_t>(c)];
                    if (dest == nullptr || src == nullptr)
                        continue;

                    for (int i = 0; i < numFrames; ++i)
                        dest[i] = static_cast<float>(src[i]);
                }
            }
            else if (replacing && effect->process_float)
            {
                effect->process_float(effect, inputs.data(), outputs.data(), numFrames);
            }
            else if (effect->process)
            {
                std::array<float*, kVst2MaxChannels> scratchOutputs {};
                for (int c = 0; c < outCh; ++c)
                    scratchOutputs[c] = scratch.data() + static_cast<std::size_t>(c * blockSize);

                effect->process(effect, inputs.data(), scratchOutputs.data(), numFrames);

                for (int c = 0; c < outCh; ++c)
                {
                    float* dest = outputs[c];
                    const float* src = scratchOutputs[c];
                    if (dest == nullptr || src == nullptr)
                        continue;

                    std::copy(src, src + numFrames, dest);
                }
            }

            latency = effect->delay;
        }

        [[nodiscard]] int latencySamples() const override { return latency; }

        bool queryRuntimeInfo(PluginInfo& ioInfo) const override
        {
            if (! effect)
                return false;

            ioInfo.ins = std::max(0, effect->num_inputs);
            ioInfo.outs = std::max(0, effect->num_outputs);
            ioInfo.latency = std::max(0, latency);
            return true;
        }

        bool getState(std::vector<std::uint8_t>& outState) override
        {
            if (! effect)
                return false;

            void* chunkPtr = nullptr;
            const auto size = effect->control(effect, VST_EFFECT_OPCODE_GET_CHUNK_DATA, 0, 0, &chunkPtr, 0.0f);
            if (size <= 0 || chunkPtr == nullptr)
                return false;

            outState.resize(static_cast<std::size_t>(size));
            std::memcpy(outState.data(), chunkPtr, outState.size());
            return true;
        }

        bool setState(const std::uint8_t* data, std::size_t len) override
        {
            if (! effect || ! data || len == 0)
                return false;

            const auto result = effect->control(effect, VST_EFFECT_OPCODE_SET_CHUNK_DATA, 0, static_cast<intptr_t>(len), const_cast<std::uint8_t*>(data), 0.0f);
            return result == 1;
        }

        [[nodiscard]] bool hasEditor() const override { return supportsEditor(); }
        std::unique_ptr<juce::Component> createEditorComponent() override;

        double currentSampleRate() const noexcept { return sampleRate > 0.0 ? sampleRate : 44100.0; }
        int currentBlockSize() const noexcept { return blockSize > 0 ? blockSize : 512; }

        bool openEditor(void* parentHandle);
        void closeEditor();
        void idleEditor();
        bool getEditorBounds(juce::Rectangle<int>& bounds) const;
        void registerEditor(Vst2EditorComponent* editor);
        void unregisterEditor(Vst2EditorComponent* editor);
        bool handleEditorResizeRequest(int width, int height);

    private:
        bool supportsEditor() const;
        void shutdown();

        SharedLibrary module;
        vst_effect_t* effect { nullptr };
        double sampleRate { 0.0 };
        int blockSize { 0 };
        bool active { false };
        bool replacing { false };
        bool supportsDoublePrecision { false };
        int latency { 0 };
        std::vector<float> scratch;
        std::vector<double> doubleInputScratch;
        std::vector<double> doubleOutputScratch;
        std::atomic<bool> editorOpen { false };
        std::atomic<Vst2EditorComponent*> activeEditor { nullptr };
    };

    class Vst2EditorComponent final : public juce::Component,
                                      private juce::Timer
    {
    public:
        explicit Vst2EditorComponent(Vst2PluginInstance& ownerIn)
            : owner(ownerIn)
        {
            setOpaque(false);
            juce::Rectangle<int> bounds;
            if (owner.getEditorBounds(bounds))
                setSize(bounds.getWidth(), bounds.getHeight());
        }

        ~Vst2EditorComponent() override
        {
            detachEditor();
        }

        void parentHierarchyChanged() override
        {
            juce::Component::parentHierarchyChanged();
            attachIfNeeded();
        }

        void visibilityChanged() override
        {
            juce::Component::visibilityChanged();
            if (isShowing())
                attachIfNeeded();
            else
                detachEditor();
        }

        void focusGained(juce::Component::FocusChangeType cause) override
        {
            juce::Component::focusGained(cause);
        }

        void focusLost(juce::Component::FocusChangeType cause) override
        {
            juce::Component::focusLost(cause);
        }

        void resized() override
        {
            juce::Component::resized();
        }

        void applyPluginRequestedSize(int width, int height)
        {
            if (width > 0 && height > 0)
                setSize(width, height);
        }

    private:
        void attachIfNeeded()
        {
            if (attached || ! isShowing())
                return;

            if (auto* peer = getPeer())
            {
                if (owner.openEditor(peer->getNativeHandle()))
                {
                    owner.registerEditor(this);
                    attached = true;
                    syncBoundsFromPlugin();
                    startTimerHz(30);
                }
            }
        }

        void detachEditor()
        {
            if (! attached)
                return;

            stopTimer();
            owner.unregisterEditor(this);
            owner.closeEditor();
            attached = false;
        }

        void syncBoundsFromPlugin()
        {
            juce::Rectangle<int> bounds;
            if (owner.getEditorBounds(bounds) && bounds.getWidth() > 0 && bounds.getHeight() > 0)
                setSize(bounds.getWidth(), bounds.getHeight());
        }

        void timerCallback() override
        {
            owner.idleEditor();
        }

        Vst2PluginInstance& owner;
        bool attached { false };
    };

    bool Vst2PluginInstance::supportsEditor() const
    {
        return effect != nullptr && (effect->flags & VST_EFFECT_FLAG_EDITOR) != 0;
    }

    std::unique_ptr<juce::Component> Vst2PluginInstance::createEditorComponent()
    {
        if (! supportsEditor())
            return {};

        return std::make_unique<Vst2EditorComponent>(*this);
    }

    bool Vst2PluginInstance::openEditor(void* parentHandle)
    {
        if (! supportsEditor() || ! effect || parentHandle == nullptr)
            return false;

        if (editorOpen.load())
            return true;

        const auto result = effect->control(effect, VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, parentHandle, 0.0f);
        if (result == 0)
            return false;

        editorOpen.store(true);
        return true;
    }

    void Vst2PluginInstance::closeEditor()
    {
        if (! effect)
            return;

        if (editorOpen.exchange(false))
            effect->control(effect, VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, nullptr, 0.0f);
    }

    void Vst2PluginInstance::idleEditor()
    {
        if (effect)
            effect->control(effect, VST_EFFECT_OPCODE_IDLE, 0, 0, nullptr, 0.0f);
    }

    bool Vst2PluginInstance::getEditorBounds(juce::Rectangle<int>& bounds) const
    {
        if (! supportsEditor())
            return false;

        vst_rect_t rect {};
        const auto result = effect->control(effect, VST_EFFECT_OPCODE_EDITOR_RECT, 0, 0, &rect, 0.0f);
        if (result == 0)
            return false;

        const int width = static_cast<int>(rect.right - rect.left);
        const int height = static_cast<int>(rect.bottom - rect.top);
        if (width <= 0 || height <= 0)
            return false;

        bounds.setBounds(0, 0, width, height);
        return true;
    }

    void Vst2PluginInstance::registerEditor(Vst2EditorComponent* editor)
    {
        activeEditor.store(editor);
    }

    void Vst2PluginInstance::unregisterEditor(Vst2EditorComponent* editor)
    {
        auto* current = activeEditor.load();
        if (current == editor)
            activeEditor.store(nullptr);
    }

    bool Vst2PluginInstance::handleEditorResizeRequest(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;

        auto* editor = activeEditor.load();
        if (editor == nullptr)
            return false;

        juce::Component::SafePointer<Vst2EditorComponent> safe(editor);
        juce::MessageManager::callAsync([safe, width, height]
        {
            if (safe != nullptr)
                safe->applyPluginRequestedSize(width, height);
        });
        return true;
    }

    void Vst2PluginInstance::shutdown()
    {
        if (! effect)
            return;

        closeEditor();
        activeEditor.store(nullptr);

        if (active)
        {
            effect->control(effect, VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 0, nullptr, 0.0f);
            active = false;
        }

        effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0.0f);
        effect->host_internal = nullptr;
        effect = nullptr;
    }

    constexpr int32_t kVstMagic = VST_FOURCC('V', 's', 't', 'P');
    constexpr int32_t kVstVersion2400 = 2400;

    intptr_t VST_FUNCTION_INTERFACE hostCallback(vst_effect_t* effect,
                                                 int32_t opcode,
                                                 int32_t index,
                                                 std::int64_t value,
                                                 const char* ptr,
                                                 float)
    {
        auto* instance = (effect != nullptr)
                             ? static_cast<Vst2PluginInstance*>(effect->host_internal)
                             : nullptr;

        switch (opcode)
        {
        case VST_HOST_OPCODE_VST_VERSION:
            return kVstVersion2400;
        case VST_HOST_OPCODE_SUPPORTS:
            if (ptr == nullptr)
                return VST_STATUS_FALSE;
            if (std::strcmp(ptr, vst_host_supports.sizeWindow) == 0
                || std::strcmp(ptr, vst_host_supports.sendVstTimeInfo) == 0)
                return VST_STATUS_TRUE;
            return VST_STATUS_FALSE;
        case VST_HOST_OPCODE_GET_TIME:
        {
            thread_local HostTimeInfo timeInfo {};
            timeInfo.samplePos = 0.0;
            timeInfo.sampleRate = instance != nullptr ? instance->currentSampleRate() : 44100.0;
            timeInfo.nanoSeconds = 0.0;
            timeInfo.ppqPos = 0.0;
            timeInfo.tempo = 120.0;
            timeInfo.barStartPos = 0.0;
            timeInfo.cycleStartPos = 0.0;
            timeInfo.cycleEndPos = 0.0;
            timeInfo.timeSigNumerator = 4;
            timeInfo.timeSigDenominator = 4;
            timeInfo.smpteOffset = 0;
            timeInfo.smpteFrameRate = 0;
            timeInfo.samplesToNextClock = 0;
            timeInfo.flags = kTimeInfoFlagTempoValid | kTimeInfoFlagTimeSigValid;
            return reinterpret_cast<intptr_t>(&timeInfo);
        }
        case VST_HOST_OPCODE_EDITOR_RESIZE:
            if (instance != nullptr)
            {
                const bool accepted = instance->handleEditorResizeRequest(index, static_cast<int>(value));
                return accepted ? VST_STATUS_TRUE : VST_STATUS_FALSE;
            }
            return VST_STATUS_FALSE;
        case VST_HOST_OPCODE_GET_SAMPLE_RATE:
            return static_cast<intptr_t>(instance != nullptr ? instance->currentSampleRate() : 44100.0);
        case VST_HOST_OPCODE_GET_BLOCK_SIZE:
            return static_cast<intptr_t>(instance != nullptr ? instance->currentBlockSize() : 512);
        case VST_HOST_OPCODE_IO_MODIFIED:
            // The plug-in reports an IO configuration change. We accept the
            // notification; runtime channel counts are refreshed lazily on
            // the next prepare().
            return VST_STATUS_TRUE;
        case VST_HOST_OPCODE_INPUT_LATENCY:
            return 0;
        case VST_HOST_OPCODE_OUTPUT_LATENCY:
            return 0;
        case VST_HOST_OPCODE_REFRESH:
            // Editor refresh requests are best-effort; the timer-driven idle
            // loop already keeps the editor in sync.
            return VST_STATUS_TRUE;
        case VST_HOST_OPCODE_VENDOR_NAME:
            if (ptr != nullptr)
            {
                std::strncpy(const_cast<char*>(ptr), "Waktaverse", 127);
                return 1;
            }
            return 0;
        case VST_HOST_OPCODE_PRODUCT_NAME:
            if (ptr != nullptr)
            {
                std::strncpy(const_cast<char*>(ptr), "VST Host", 127);
                return 1;
            }
            return 0;
        case VST_HOST_OPCODE_VENDOR_VERSION:
            return 0x00010000; // 0.1.0
        default:
            break;
        }

        return 0;
    }
}

std::unique_ptr<PluginInstance> loadVst2(const PluginInfo& info)
{
    if (info.path.empty())
    {
        logPluginLoadFailure(info, "Stored module path is empty");
        return nullptr;
    }

    SharedLibrary module;
    if (! module.load(info.path))
    {
        juce::String reason = module.getLastError();
        if (reason.isEmpty())
            reason = "Module load failed";
        logPluginLoadFailure(info,
                             "Unable to load '" + pathToString(info.path) + "': " + reason);
        return nullptr;
    }

    using EntryProc = vst_effect_t* (VST_FUNCTION_INTERFACE*)(vst_host_callback_t);
    EntryProc entry = nullptr;

    constexpr std::array<const char*, 3> entryPoints { "VSTPluginMain", "main", "main_macho" };
    for (const auto* name : entryPoints)
    {
        if (auto* sym = module.getSymbol(name))
        {
            entry = reinterpret_cast<EntryProc>(sym);
            break;
        }
    }

    if (! entry)
    {
        juce::String names;
        for (const auto* name : entryPoints)
        {
            if (names.isNotEmpty())
                names += ", ";
            names += name;
        }

        logPluginLoadFailure(info, "Could not locate entry point (" + names + ")");
        return nullptr;
    }

    auto* effect = entry(&hostCallback);
    if (! effect)
    {
        logPluginLoadFailure(info, "Entry point returned a null effect pointer");
        return nullptr;
    }

    if (effect->magic_number != kVstMagic)
    {
        logPluginLoadFailure(info, "Entry point returned invalid VST magic");
        return nullptr;
    }

    effect->control(effect, VST_EFFECT_OPCODE_CREATE, 0, 0, nullptr, 0.0f);

    if (effect->num_outputs <= 0)
    {
        logPluginLoadFailure(info, "Channel configuration reports zero outputs");
        effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0.0f);
        return nullptr;
    }

    if (effect->num_inputs != info.ins || effect->num_outputs != info.outs)
    {
        juce::String message("VST2 channel configuration mismatch");
        if (! info.name.empty())
            message += " (" + juce::String(info.name) + ")";
        message += ": expected ";
        message += juce::String(info.ins) + "/" + juce::String(info.outs);
        message += ", plug-in reports ";
        message += juce::String(effect->num_inputs) + "/" + juce::String(effect->num_outputs);
        message += " (continuing)";
        juce::Logger::writeToLog(message.trimEnd());
    }

    return std::make_unique<Vst2PluginInstance>(std::move(module), effect);
}

} // namespace host::plugin
