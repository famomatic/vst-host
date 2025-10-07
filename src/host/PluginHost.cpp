#include "host/PluginHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

#if !defined(_WIN32) && !defined(__cdecl)
 #define __cdecl
#endif

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/vsttypes.h>

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
#if defined(_WIN32)
    using ModuleHandle = HMODULE;
#else
    using ModuleHandle = void*;
#endif

    class SharedLibrary
    {
    public:
        SharedLibrary() = default;
        explicit SharedLibrary(const std::filesystem::path& p) { load(p); }
        ~SharedLibrary() { unload(); }

        SharedLibrary(const SharedLibrary&) = delete;
        SharedLibrary& operator=(const SharedLibrary&) = delete;

        SharedLibrary(SharedLibrary&& other) noexcept
            : handle(other.handle)
        {
            other.handle = {};
        }

        SharedLibrary& operator=(SharedLibrary&& other) noexcept
        {
            if (this != &other)
            {
                unload();
                handle = other.handle;
                other.handle = {};
            }
            return *this;
        }

        bool load(const std::filesystem::path& p)
        {
            unload();
#if defined(_WIN32)
            handle = ::LoadLibraryW(p.wstring().c_str());
#else
            handle = ::dlopen(p.string().c_str(), RTLD_NOW);
#endif
            return handle != nullptr;
        }

        void unload()
        {
            if (! handle)
                return;
#if defined(_WIN32)
            ::FreeLibrary(handle);
#else
            ::dlclose(handle);
#endif
            handle = {};
        }

        [[nodiscard]] void* getSymbol(const char* name) const
        {
            if (! handle)
                return nullptr;
#if defined(_WIN32)
            return reinterpret_cast<void*>(::GetProcAddress(handle, name));
#else
            return ::dlsym(handle, name);
#endif
        }

        [[nodiscard]] bool isLoaded() const noexcept { return handle != nullptr; }

    private:
        ModuleHandle handle {};
    };

    class MemoryStream final : public Steinberg::IBStream
    {
    public:
        explicit MemoryStream(std::vector<std::uint8_t>& target)
            : writeBuffer(&target)
        {
        }

        MemoryStream(const std::uint8_t* data, std::size_t sizeIn)
            : readBuffer(data)
            , readSize(sizeIn)
        {
        }

        // FUnknown
        Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount; }

        Steinberg::uint32 PLUGIN_API release() override
        {
            auto newCount = --refCount;
            if (newCount == 0)
                delete this;
            return newCount;
        }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
        {
            if (std::memcmp(iid, Steinberg::IBStream::iid, sizeof(Steinberg::TUID)) == 0)
            {
                addRef();
                *obj = static_cast<Steinberg::IBStream*>(this);
                return Steinberg::kResultOk;
            }

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        // IBStream
        Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesRead) override
        {
            if (! readBuffer)
            {
                if (numBytesRead)
                    *numBytesRead = 0;
                return Steinberg::kResultFalse;
            }

            const auto bytesAvailable = static_cast<Steinberg::int64>(readSize) - position;
            const auto toRead = static_cast<Steinberg::int32>(std::max<Steinberg::int64>(0, std::min<Steinberg::int64>(bytesAvailable, numBytes)));
            if (toRead > 0)
                std::memcpy(buffer, readBuffer + position, static_cast<std::size_t>(toRead));

            position += toRead;
            if (numBytesRead)
                *numBytesRead = toRead;
            return toRead == numBytes ? Steinberg::kResultOk : Steinberg::kResultTrue;
        }

        Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesWritten) override
        {
            if (! writeBuffer)
            {
                if (numBytesWritten)
                    *numBytesWritten = 0;
                return Steinberg::kResultFalse;
            }

            auto* src = static_cast<std::uint8_t*>(buffer);
            if (position + numBytes > static_cast<Steinberg::int64>(writeBuffer->size()))
                writeBuffer->resize(static_cast<std::size_t>(position + numBytes));

            std::memcpy(writeBuffer->data() + position, src, static_cast<std::size_t>(numBytes));
            position += numBytes;
            if (numBytesWritten)
                *numBytesWritten = numBytes;
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode, Steinberg::int64* result) override
        {
            Steinberg::int64 newPos = position;
            switch (mode)
            {
            case Steinberg::IBStream::kIBSeekSet: newPos = pos; break;
            case Steinberg::IBStream::kIBSeekCur: newPos += pos; break;
            case Steinberg::IBStream::kIBSeekEnd:
                if (writeBuffer)
                    newPos = static_cast<Steinberg::int64>(writeBuffer->size()) + pos;
                else
                    newPos = static_cast<Steinberg::int64>(readSize) + pos;
                break;
            default: return Steinberg::kResultFalse;
            }

            if (newPos < 0)
                newPos = 0;

            if (writeBuffer && newPos > static_cast<Steinberg::int64>(writeBuffer->size()))
                writeBuffer->resize(static_cast<std::size_t>(newPos));

            position = newPos;
            if (result)
                *result = position;
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override
        {
            if (! pos)
                return Steinberg::kInvalidArgument;
            *pos = position;
            return Steinberg::kResultOk;
        }

    private:
        std::atomic<Steinberg::uint32> refCount { 1 };
        std::vector<std::uint8_t>* writeBuffer { nullptr };
        const std::uint8_t* readBuffer { nullptr };
        std::size_t readSize { 0 };
        Steinberg::int64 position { 0 };
    };

    class Vst3PluginInstance final : public PluginInstance
    {
    public:
        static constexpr int kMaxChannels = 8;

        Vst3PluginInstance(SharedLibrary&& moduleIn,
                           Steinberg::Vst::IComponent* componentIn,
                           Steinberg::Vst::IAudioProcessor* processorIn,
                           Steinberg::Vst::IEditController* controllerIn,
                           int inChannels,
                           int outChannels,
                           bool alreadyInitialized)
            : module(std::move(moduleIn))
            , component(componentIn)
            , processor(processorIn)
            , controller(controllerIn)
            , maxInputs(std::clamp(inChannels, 0, kMaxChannels))
            , maxOutputs(std::clamp(outChannels, 0, kMaxChannels))
            , initialized(alreadyInitialized)
        {
        }

        ~Vst3PluginInstance() override { shutdown(); }

        void prepare(double sr, int block) override
        {
            if (! component || ! processor)
                return;

            ensureInitialized();

            if (processing)
            {
                processor->setProcessing(false);
                processing = false;
            }

            if (active)
            {
                component->setActive(false);
                active = false;
            }

            Steinberg::Vst::SpeakerArrangement inArr = Steinberg::Vst::SpeakerArr::kStereo;
            Steinberg::Vst::SpeakerArrangement outArr = Steinberg::Vst::SpeakerArr::kStereo;
            if (processor->setBusArrangements(&inArr, 1, &outArr, 1) != Steinberg::kResultOk)
                return;

            setup.processMode = Steinberg::Vst::kRealtime;
            setup.symbolicSampleSize = Steinberg::Vst::kSample32;
            setup.maxSamplesPerBlock = block;
            setup.sampleRate = sr;
            if (processor->setupProcessing(setup) != Steinberg::kResultOk)
                return;

            component->setActive(true);
            active = true;

            processor->setProcessing(true);
            processing = true;

            latency = static_cast<int>(processor->getLatencySamples());
        }

        void process(float** in, int inCh, float** out, int outCh, int numFrames) override
        {
            if (! processor || ! processing || ! in || ! out)
                return;

            std::array<float*, 8> inputPtrs {};
            std::array<float*, 8> outputPtrs {};
            const int usedInputs = std::min(maxInputs, static_cast<int>(inputPtrs.size()));
            const int usedOutputs = std::min(maxOutputs, static_cast<int>(outputPtrs.size()));

            if (inCh < usedInputs || outCh < usedOutputs)
                return;

            for (int ch = 0; ch < usedInputs; ++ch)
                inputPtrs[static_cast<std::size_t>(ch)] = in[ch];

            for (int ch = 0; ch < usedOutputs; ++ch)
                outputPtrs[static_cast<std::size_t>(ch)] = out[ch];

            Steinberg::Vst::AudioBusBuffers inputs[1] {};
            Steinberg::Vst::AudioBusBuffers outputs[1] {};

            inputs[0].numChannels = usedInputs;
            inputs[0].silenceFlags = 0;
            inputs[0].channelBuffers32 = inputPtrs.data();

            outputs[0].numChannels = usedOutputs;
            outputs[0].silenceFlags = 0;
            outputs[0].channelBuffers32 = outputPtrs.data();

            Steinberg::Vst::ProcessData data {};
            data.numInputs = 1;
            data.numOutputs = 1;
            data.numSamples = numFrames;
            data.symbolicSampleSize = Steinberg::Vst::kSample32;
            data.inputs = inputs;
            data.outputs = outputs;
            // TODO: Attach event queues and parameter changes when automation is supported.

            processor->process(data);
            latency = static_cast<int>(processor->getLatencySamples());
        }

        [[nodiscard]] int latencySamples() const override { return latency; }

        bool getState(std::vector<std::uint8_t>& outState) override
        {
            if (! component)
                return false;

            outState.clear();
            auto* stream = new MemoryStream(outState);
            const auto result = component->getState(stream);
            stream->release();
            if (result == Steinberg::kResultOk)
                return true;

            if (controller)
            {
                outState.clear();
                stream = new MemoryStream(outState);
                const auto ctrlResult = controller->getState(stream);
                stream->release();
                return ctrlResult == Steinberg::kResultOk;
            }

            return false;
        }

        bool setState(const std::uint8_t* data, std::size_t len) override
        {
            if (! component || ! data || len == 0)
                return false;

            auto* stream = new MemoryStream(data, len);
            const auto result = component->setState(stream);
            stream->release();
            if (result == Steinberg::kResultOk)
                return true;

            if (controller)
            {
                stream = new MemoryStream(data, len);
                const auto ctrlResult = controller->setState(stream);
                stream->release();
                return ctrlResult == Steinberg::kResultOk;
            }

            return false;
        }

    private:
        void ensureInitialized()
        {
            if (initialized || ! component)
                return;

            if (component->initialize(nullptr) == Steinberg::kResultOk)
            {
                component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
                component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
                initialized = true;
            }
        }

        void shutdown()
        {
            if (processor && processing)
            {
                processor->setProcessing(false);
                processing = false;
            }

            if (component && active)
            {
                component->setActive(false);
                active = false;
            }

            if (controller)
            {
                controller->setComponentHandler(nullptr);
                controller->release();
                controller = nullptr;
            }

            if (component)
            {
                if (initialized)
                    component->terminate();
                component->release();
                component = nullptr;
            }

            if (processor)
            {
                processor->release();
                processor = nullptr;
            }
        }

        SharedLibrary module;
        Steinberg::Vst::IComponent* component { nullptr };
        Steinberg::Vst::IAudioProcessor* processor { nullptr };
        Steinberg::Vst::IEditController* controller { nullptr };
        Steinberg::Vst::ProcessSetup setup {};
        bool initialized { false };
        bool active { false };
        bool processing { false };
        int latency { 0 };
        int maxInputs { 0 };
        int maxOutputs { 0 };
    };

    constexpr int kVst2MaxChannels = 32;

    class Vst2PluginInstance final : public PluginInstance
    {
    public:
        Vst2PluginInstance(SharedLibrary&& moduleIn, vst_effect_t* effectIn)
            : module(std::move(moduleIn))
            , effect(effectIn)
        {
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

            scratch.resize(static_cast<std::size_t>(std::max(1, effect->num_outputs) * block));
            latency = effect->delay;
        }

        void process(float** in, int inCh, float** out, int outCh, int numFrames) override
        {
            if (! effect || ! active || ! in || ! out)
                return;

            if (numFrames > blockSize)
                return;

            if (effect->num_inputs != inCh || effect->num_outputs != outCh)
                return;

            std::array<float*, kVst2MaxChannels> inputs {};
            std::array<float*, kVst2MaxChannels> outputs {};

            for (int c = 0; c < outCh; ++c)
            {
                inputs[c] = (c < inCh && in[c]) ? in[c] : nullptr;
                outputs[c] = out[c];
            }

            if (replacing && effect->process_float)
            {
                // TODO: Support double precision processing when available.
                effect->process_float(effect, inputs.data(), out, numFrames);
            }
            else if (effect->process)
            {
                for (int c = 0; c < outCh; ++c)
                    outputs[c] = scratch.data() + static_cast<std::size_t>(c * blockSize);

                effect->process(effect, inputs.data(), outputs.data(), numFrames);

                for (int c = 0; c < outCh; ++c)
                    std::copy(outputs[c], outputs[c] + numFrames, out[c]);
            }

            latency = effect->delay;
        }

        [[nodiscard]] int latencySamples() const override { return latency; }

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

    private:
        void shutdown()
        {
            if (! effect)
                return;

            if (active)
            {
                effect->control(effect, VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 0, nullptr, 0.0f);
                active = false;
            }

            effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0.0f);
            effect = nullptr;
        }

        SharedLibrary module;
        vst_effect_t* effect { nullptr };
        double sampleRate { 0.0 };
        int blockSize { 0 };
        bool active { false };
        bool replacing { false };
        int latency { 0 };
        std::vector<float> scratch;
    };

    constexpr int32_t kVstMagic = VST_FOURCC('V', 's', 't', 'P');
    constexpr int32_t kVstVersion2400 = 2400;

    intptr_t VST_FUNCTION_INTERFACE hostCallback(vst_effect_t*, int32_t opcode, int32_t, std::int64_t, const char*, float)
    {
        if (opcode == VST_HOST_OPCODE_VST_VERSION)
            return kVstVersion2400;
        return 0;
    }
}

std::unique_ptr<PluginInstance> loadVst3(const PluginInfo& info)
{
    SharedLibrary module;
    if (! module.load(info.path))
        return nullptr;

    using GetFactoryProc = Steinberg::IPluginFactory*(*)();
    auto* symbol = module.getSymbol("GetPluginFactory");
    if (! symbol)
        return nullptr;

    auto factory = reinterpret_cast<GetFactoryProc>(symbol)();
    if (! factory)
        return nullptr;

    Steinberg::FUID requestedClassId;
    const bool hasRequestedId = ! info.id.empty() && requestedClassId.fromString(info.id.c_str());

    Steinberg::PClassInfo classInfo {};
    Steinberg::Vst::IComponent* component = nullptr;
    Steinberg::Vst::IAudioProcessor* processor = nullptr;
    Steinberg::Vst::IEditController* controller = nullptr;

    const auto count = factory->countClasses();
    for (Steinberg::int32 i = 0; i < count; ++i)
    {
        if (factory->getClassInfo(i, &classInfo) != Steinberg::kResultOk)
            continue;

        if (std::strcmp(classInfo.category, kVstAudioEffectClass) != 0)
            continue;

        Steinberg::FUID currentId(classInfo.cid);
        if (hasRequestedId && !(currentId == requestedClassId))
            continue;

        if (factory->createInstance(classInfo.cid, Steinberg::Vst::IComponent::iid, reinterpret_cast<void**>(&component)) != Steinberg::kResultOk || ! component)
            continue;

        if (component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, reinterpret_cast<void**>(&processor)) != Steinberg::kResultOk || ! processor)
        {
            component->release();
            component = nullptr;
            continue;
        }

        if (factory->createInstance(classInfo.cid, Steinberg::Vst::IEditController::iid, reinterpret_cast<void**>(&controller)) == Steinberg::kResultOk && controller)
        {
            if (controller->initialize(nullptr) != Steinberg::kResultOk)
            {
                controller->release();
                controller = nullptr;
            }
        }
        break;
    }

    factory->release();

    if (! component || ! processor)
        return nullptr;

    if (component->initialize(nullptr) != Steinberg::kResultOk)
    {
        processor->release();
        component->release();
        if (controller)
            controller->release();
        return nullptr;
    }

    Steinberg::Vst::BusInfo inBus {};
    Steinberg::Vst::BusInfo outBus {};
    if (component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput) <= 0 || component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput) <= 0 ||
        component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, inBus) != Steinberg::kResultOk ||
        component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, outBus) != Steinberg::kResultOk ||
        inBus.channelCount != info.ins || outBus.channelCount != info.outs)
    {
        component->terminate();
        processor->release();
        component->release();
        if (controller)
            controller->release();
        return nullptr;
    }

    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);

    return std::make_unique<Vst3PluginInstance>(std::move(module), component, processor, controller, info.ins, info.outs, true);
}

std::unique_ptr<PluginInstance> loadVst2(const PluginInfo& info)
{
    SharedLibrary module;
    if (! module.load(info.path))
        return nullptr;

    using EntryProc = vst_effect_t* (VST_FUNCTION_INTERFACE*)(vst_host_callback_t);
    EntryProc entry = nullptr;

    if (auto* sym = module.getSymbol("VSTPluginMain"))
        entry = reinterpret_cast<EntryProc>(sym);
    else if (auto* sym = module.getSymbol("main"))
        entry = reinterpret_cast<EntryProc>(sym);
    else if (auto* sym = module.getSymbol("main_macho"))
        entry = reinterpret_cast<EntryProc>(sym);

    if (! entry)
        return nullptr;

    auto* effect = entry(&hostCallback);
    if (! effect || effect->magic_number != kVstMagic)
        return nullptr;

    effect->control(effect, VST_EFFECT_OPCODE_CREATE, 0, 0, nullptr, 0.0f);

    if (effect->num_inputs != info.ins || effect->num_outputs != info.outs)
    {
        effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0.0f);
        return nullptr;
    }

    return std::make_unique<Vst2PluginInstance>(std::move(module), effect);
}

} // namespace host::plugin
