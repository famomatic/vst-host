#include "host/PluginHost.h"
#include "host/SharedLibrary.h"
#include "host/PluginHostVst3Editor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <cctype>
#include <cstdint>
#include <vector>
#include <filesystem>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

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
#include <pluginterfaces/base/smartpointer.h>
#include <pluginterfaces/base/ustring.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivstpluginterfacesupport.h>
#include <pluginterfaces/vst/ivstcontextmenu.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vsttypes.h>

#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
namespace host::plugin
{
namespace
{
    class SimpleHostApplication final : public Steinberg::Vst::IHostApplication,
                                        public Steinberg::Vst::IPlugInterfaceSupport
    {
    public:
        SimpleHostApplication() = default;
        ~SimpleHostApplication() = default;

        Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
        {
            if (name == nullptr)
                return Steinberg::kInvalidArgument;

            Steinberg::UString wrapper(name, 128);
            wrapper.fromAscii("VST Host");
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API isPlugInterfaceSupported(const Steinberg::TUID iid) override
        {
            if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid)
                || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler2::iid)
                || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler3::iid)
                || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IMessage::iid)
                || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IAttributeList::iid)
                || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IHostApplication::iid)
                || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IPlugInterfaceSupport::iid))
            {
                return Steinberg::kResultTrue;
            }

            return Steinberg::kResultFalse;
        }

        Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID, Steinberg::TUID iid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            *obj = nullptr;

            const auto requested = Steinberg::FUID::fromTUID(iid);
            if (requested == Steinberg::Vst::IMessage::iid)
            {
                auto message = Steinberg::owned(new Steinberg::Vst::HostMessage());
                if (! message)
                    return Steinberg::kOutOfMemory;

                *obj = message.take();
                return Steinberg::kResultTrue;
            }

            if (requested == Steinberg::Vst::IAttributeList::iid)
            {
                auto attributes = Steinberg::Vst::HostAttributeList::make();
                if (! attributes)
                    return Steinberg::kOutOfMemory;

                *obj = attributes.take();
                return Steinberg::kResultTrue;
            }

            return Steinberg::kResultFalse;
        }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IHostApplication)
            QUERY_INTERFACE(iid, obj, Steinberg::Vst::IHostApplication::iid, Steinberg::Vst::IHostApplication)
            QUERY_INTERFACE(iid, obj, Steinberg::Vst::IPlugInterfaceSupport::iid, Steinberg::Vst::IPlugInterfaceSupport)

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return ++refCount;
        }

        Steinberg::uint32 PLUGIN_API release() override
        {
            auto remaining = --refCount;
            if (remaining == 0)
                delete this;
            return remaining;
        }

    private:
        std::atomic<uint32_t> refCount { 1 };
    };

    class SimpleComponentHandler final : public Steinberg::Vst::IComponentHandler,
                                         public Steinberg::Vst::IComponentHandler2,
                                         public Steinberg::Vst::IComponentHandler3
    {
    public:
        SimpleComponentHandler() = default;
        ~SimpleComponentHandler() = default;

        Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override { return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }

        Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool) override { return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString) override { return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }
        Steinberg::Vst::IContextMenu* PLUGIN_API createContextMenu(Steinberg::IPlugView*, const Steinberg::Vst::ParamID*) override { return nullptr; }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IComponentHandler)
            QUERY_INTERFACE(iid, obj, Steinberg::Vst::IComponentHandler::iid, Steinberg::Vst::IComponentHandler)
            QUERY_INTERFACE(iid, obj, Steinberg::Vst::IComponentHandler2::iid, Steinberg::Vst::IComponentHandler2)
            QUERY_INTERFACE(iid, obj, Steinberg::Vst::IComponentHandler3::iid, Steinberg::Vst::IComponentHandler3)

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return ++refCount;
        }

        Steinberg::uint32 PLUGIN_API release() override
        {
            auto remaining = --refCount;
            if (remaining == 0)
                delete this;
            return remaining;
        }

    private:
        std::atomic<uint32_t> refCount { 1 };
    };

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



    [[nodiscard]] std::string toLowerCopy(std::string input)
    {
        std::transform(input.begin(), input.end(), input.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return input;
    }

    [[nodiscard]] bool isLikelyMacOsBinary(const std::filesystem::path& candidate)
    {
        const auto parent = toLowerCopy(candidate.parent_path().filename().string());
        return parent == "macos";
    }

    [[nodiscard]] bool isCandidateVst3ModuleFile(const std::filesystem::path& path)
    {
        auto ext = toLowerCopy(path.extension().string());
#if defined(_WIN32)
        return ext == ".vst3";
#elif defined(__APPLE__)
        if (ext == ".dylib")
            return true;

        if (ext.empty() && isLikelyMacOsBinary(path))
            return true;
#else
        return ext == ".so";
#endif
        return false;
    }

    [[nodiscard]] std::optional<std::filesystem::path> findModuleInDirectory(const std::filesystem::path& root)
    {
        std::error_code ec;
        const auto options = std::filesystem::directory_options::follow_directory_symlink
                             | std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; ! ec && it != end; ++it)
        {
            std::error_code statusEc;
            if (! it->is_regular_file(statusEc) || statusEc)
                continue;

            const auto candidate = it->path();
            if (isCandidateVst3ModuleFile(candidate))
                return candidate;
        }

        if (ec)
            return std::nullopt;

        return std::nullopt;
    }

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

    [[nodiscard]] std::optional<std::filesystem::path> resolveVst3ModulePath(const std::filesystem::path& providedPath)
    {
        if (providedPath.empty())
            return std::nullopt;

        std::error_code ec;
        if (! std::filesystem::exists(providedPath, ec) || ec)
            return std::nullopt;

        if (std::filesystem::is_regular_file(providedPath, ec) && ! ec)
        {
            if (isCandidateVst3ModuleFile(providedPath))
                return providedPath;
        }

        if (! std::filesystem::is_directory(providedPath, ec) || ec)
            return std::nullopt;

        std::vector<std::filesystem::path> searchRoots;
        auto addSearchRoot = [&](const std::filesystem::path& candidate)
        {
            if (candidate.empty())
                return;

            std::error_code existsEc;
            if (! std::filesystem::exists(candidate, existsEc) || existsEc)
                return;

            if (! std::filesystem::is_directory(candidate, existsEc) || existsEc)
                return;

            if (std::find(searchRoots.begin(), searchRoots.end(), candidate) == searchRoots.end())
                searchRoots.push_back(candidate);
        };

        const auto filenameLower = toLowerCopy(providedPath.filename().string());
        if (filenameLower == "contents" || filenameLower == "resources" || filenameLower == "macos")
        {
            const auto parent = providedPath.parent_path();
            addSearchRoot(parent);
            const auto grandParent = parent.parent_path();
            addSearchRoot(grandParent);
        }

        const std::filesystem::path contentsPath = providedPath / "Contents";
        addSearchRoot(contentsPath);

        const std::array<std::string, 4> preferredDirs { "x86_64-win", "x86_64-linux", "MacOS", "Resources" };
        for (const auto& name : preferredDirs)
            addSearchRoot(contentsPath / name);

        std::error_code dirEc;
        for (std::filesystem::directory_iterator dirIt(contentsPath, std::filesystem::directory_options::skip_permission_denied, dirEc), end;
             ! dirEc && dirIt != end;
             ++dirIt)
            addSearchRoot(dirIt->path());

        addSearchRoot(providedPath);

        for (const auto& root : searchRoots)
        {
            if (const auto found = findModuleInDirectory(root))
                return found;
        }

        return std::nullopt;
    }



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

    class EventList final : public Steinberg::Vst::IEventList
    {
    public:
        EventList() = default;

        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            if (std::memcmp(iid, Steinberg::Vst::IEventList::iid, sizeof(Steinberg::TUID)) == 0
                || std::memcmp(iid, Steinberg::FUnknown::iid, sizeof(Steinberg::TUID)) == 0)
            {
                *obj = static_cast<Steinberg::Vst::IEventList*>(this);
                addRef();
                return Steinberg::kResultOk;
            }

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::int32 PLUGIN_API getEventCount() override
        {
            return static_cast<Steinberg::int32>(events_.size());
        }

        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override
        {
            if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
                return Steinberg::kInvalidArgument;

            e = events_[static_cast<std::size_t>(index)];
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override
        {
            events_.push_back(e);
            return Steinberg::kResultOk;
        }

        void clear() { events_.clear(); }

    private:
        std::vector<Steinberg::Vst::Event> events_;
    };

    class ParamValueQueue final : public Steinberg::Vst::IParamValueQueue
    {
    public:
        explicit ParamValueQueue(Steinberg::Vst::ParamID pid)
            : paramId_(pid)
        {
        }

        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            if (std::memcmp(iid, Steinberg::Vst::IParamValueQueue::iid, sizeof(Steinberg::TUID)) == 0
                || std::memcmp(iid, Steinberg::FUnknown::iid, sizeof(Steinberg::TUID)) == 0)
            {
                *obj = static_cast<Steinberg::Vst::IParamValueQueue*>(this);
                addRef();
                return Steinberg::kResultOk;
            }

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }

        Steinberg::int32 PLUGIN_API getPointCount() override
        {
            return static_cast<Steinberg::int32>(points_.size());
        }

        Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset, Steinberg::Vst::ParamValue& value) override
        {
            if (index < 0 || index >= static_cast<Steinberg::int32>(points_.size()))
                return Steinberg::kInvalidArgument;

            const auto& point = points_[static_cast<std::size_t>(index)];
            sampleOffset = point.offset;
            value = point.value;
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override
        {
            points_.push_back({ sampleOffset, value });
            index = static_cast<Steinberg::int32>(points_.size()) - 1;
            return Steinberg::kResultOk;
        }

        void clear() { points_.clear(); }

    private:
        struct Point
        {
            Steinberg::int32 offset { 0 };
            Steinberg::Vst::ParamValue value { 0.0 };
        };

        Steinberg::Vst::ParamID paramId_;
        std::vector<Point> points_;
    };

    class ParameterChanges final : public Steinberg::Vst::IParameterChanges
    {
    public:
        ParameterChanges() = default;

        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            if (std::memcmp(iid, Steinberg::Vst::IParameterChanges::iid, sizeof(Steinberg::TUID)) == 0
                || std::memcmp(iid, Steinberg::FUnknown::iid, sizeof(Steinberg::TUID)) == 0)
            {
                *obj = static_cast<Steinberg::Vst::IParameterChanges*>(this);
                addRef();
                return Steinberg::kResultOk;
            }

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::int32 PLUGIN_API getParameterCount() override
        {
            return static_cast<Steinberg::int32>(queues_.size());
        }

        Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
        {
            if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
                return nullptr;

            return queues_[static_cast<std::size_t>(index)].get();
        }

        Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override
        {
            for (std::size_t i = 0; i < queues_.size(); ++i)
            {
                if (queues_[i]->getParameterId() == id)
                {
                    index = static_cast<Steinberg::int32>(i);
                    return queues_[i].get();
                }
            }

            auto queue = std::make_unique<ParamValueQueue>(id);
            auto* raw = queue.get();
            queues_.push_back(std::move(queue));
            index = static_cast<Steinberg::int32>(queues_.size()) - 1;
            return raw;
        }

        void clear()
        {
            queues_.clear();
        }

    private:
        std::vector<std::unique_ptr<ParamValueQueue>> queues_;
    };

    class Vst3PluginInstance;
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
                           Steinberg::Vst::SpeakerArrangement inputArrangementIn,
                           Steinberg::Vst::SpeakerArrangement outputArrangementIn,
                           bool alreadyInitialized,
                           Steinberg::IPtr<SimpleHostApplication> hostContextIn,
                           Steinberg::IPtr<SimpleComponentHandler> componentHandlerIn,
                           bool controllerIsInitialised,
                           bool connectionPointsLinked)
            : module(std::move(moduleIn))
            , component(componentIn)
            , processor(processorIn)
            , controller(controllerIn)
            , maxInputs(std::clamp(inChannels, 0, kMaxChannels))
            , maxOutputs(std::clamp(outChannels, 0, kMaxChannels))
            , hasInputBus(inChannels > 0)
            , inputArrangement(inputArrangementIn)
            , outputArrangement(outputArrangementIn)
            , initialized(alreadyInitialized)
            , controllerInitialized(controllerIsInitialised)
            , connectionPointsConnected(connectionPointsLinked)
            , hostContext(std::move(hostContextIn))
            , componentHandler(std::move(componentHandlerIn))
            , inputEvents_(std::make_unique<EventList>())
            , outputEvents_(std::make_unique<EventList>())
            , inputParameterChanges_(std::make_unique<ParameterChanges>())
            , outputParameterChanges_(std::make_unique<ParameterChanges>())
        {
        }

        ~Vst3PluginInstance() override { shutdown(); }

        [[nodiscard]] bool hasEditor() const override { return controller != nullptr; }

        [[nodiscard]] bool isEditorResizable() const override
        {
            return controller != nullptr && editorResizableSupported;
        }

        std::unique_ptr<juce::Component> createEditorComponent() override
        {
            return vst3editor::createEditorComponent(
                [this]() { return createEditorView(); },
                [this](Steinberg::IPlugView* view) { releaseEditorView(view); });
        }

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

            Steinberg::Vst::SpeakerArrangement inArr = inputArrangement;
            Steinberg::Vst::SpeakerArrangement outArr = outputArrangement;
            Steinberg::Vst::SpeakerArrangement* inArrPtr = hasInputBus ? &inArr : nullptr;
            const Steinberg::int32 inBusCount = hasInputBus ? 1 : 0;
            if (processor->setBusArrangements(inArrPtr, inBusCount, &outArr, 1) != Steinberg::kResultOk)
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
            if (! processor || ! processing || ! out)
                return;

            if (hasInputBus && ! in)
                return;

            std::array<float*, 8> inputPtrs {};
            std::array<float*, 8> outputPtrs {};
            const int usedInputs = hasInputBus ? std::min(maxInputs, static_cast<int>(inputPtrs.size())) : 0;
            const int usedOutputs = std::min(maxOutputs, static_cast<int>(outputPtrs.size()));

            if (usedOutputs <= 0)
                return;

            if (inCh < usedInputs || outCh < usedOutputs)
                return;

            for (int ch = 0; ch < usedInputs; ++ch)
                inputPtrs[static_cast<std::size_t>(ch)] = in[ch];

            for (int ch = 0; ch < usedOutputs; ++ch)
                outputPtrs[static_cast<std::size_t>(ch)] = out[ch];

            Steinberg::Vst::AudioBusBuffers inputBus {};
            Steinberg::Vst::AudioBusBuffers outputBus {};

            Steinberg::Vst::AudioBusBuffers* inputArray = nullptr;
            Steinberg::int32 numInputBuses = 0;
            if (hasInputBus)
            {
                inputBus.numChannels = usedInputs;
                inputBus.silenceFlags = 0;
                inputBus.channelBuffers32 = inputPtrs.data();
                inputArray = &inputBus;
                numInputBuses = 1;
            }

            outputBus.numChannels = usedOutputs;
            outputBus.silenceFlags = 0;
            outputBus.channelBuffers32 = outputPtrs.data();

            Steinberg::Vst::ProcessData data {};
            data.numInputs = numInputBuses;
            data.numOutputs = 1;
            data.numSamples = numFrames;
            data.symbolicSampleSize = Steinberg::Vst::kSample32;
            data.inputs = inputArray;
            data.outputs = &outputBus;
            if (inputEvents_)
                inputEvents_->clear();
            if (outputEvents_)
                outputEvents_->clear();
            if (inputParameterChanges_)
                inputParameterChanges_->clear();
            if (outputParameterChanges_)
                outputParameterChanges_->clear();

            data.inputEvents = inputEvents_.get();
            data.outputEvents = outputEvents_.get();
            data.inputParameterChanges = inputParameterChanges_.get();
            data.outputParameterChanges = outputParameterChanges_.get();

            processor->process(data);
            latency = static_cast<int>(processor->getLatencySamples());
        }

        [[nodiscard]] int latencySamples() const override { return latency; }

        bool queryRuntimeInfo(PluginInfo& ioInfo) const override
        {
            ioInfo.ins = std::max(0, maxInputs);
            ioInfo.outs = std::max(0, maxOutputs);
            ioInfo.latency = std::max(0, latency);
            return true;
        }

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

            if (component->initialize(hostContext ? static_cast<Steinberg::Vst::IHostApplication*>(hostContext.get()) : nullptr) == Steinberg::kResultOk)
            {
                if (hasInputBus)
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

            if (connectionPointsConnected && component && controller)
            {
                Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection(component);
                Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> controllerConnection(controller);
                if (componentConnection && controllerConnection)
                {
                    componentConnection->disconnect(controllerConnection);
                    controllerConnection->disconnect(componentConnection);
                }
                connectionPointsConnected = false;
            }

            if (controller)
            {
                if (controllerInitialized)
                {
                    controller->terminate();
                    controllerInitialized = false;
                }
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
        bool hasInputBus { false };
        Steinberg::Vst::SpeakerArrangement inputArrangement { Steinberg::Vst::SpeakerArr::kEmpty };
        Steinberg::Vst::SpeakerArrangement outputArrangement { Steinberg::Vst::SpeakerArr::kStereo };
        bool initialized { false };
        bool active { false };
        bool processing { false };
        int latency { 0 };
        int maxInputs { 0 };
        int maxOutputs { 0 };
        bool controllerInitialized { false };
        bool connectionPointsConnected { false };
        bool editorResizableSupported { false };
        Steinberg::IPtr<SimpleHostApplication> hostContext;
        Steinberg::IPtr<SimpleComponentHandler> componentHandler;
        std::unique_ptr<EventList> inputEvents_;
        std::unique_ptr<EventList> outputEvents_;
        std::unique_ptr<ParameterChanges> inputParameterChanges_;
        std::unique_ptr<ParameterChanges> outputParameterChanges_;

        Steinberg::IPlugView* createEditorView()
        {
            if (! controller)
                return nullptr;

            auto* view = controller->createView(Steinberg::Vst::ViewType::kEditor);
            editorResizableSupported = (view != nullptr && view->canResize() == Steinberg::kResultTrue);
            return view;
        }

        void releaseEditorView(Steinberg::IPlugView* viewToRelease)
        {
            if (viewToRelease != nullptr)
                viewToRelease->release();
        }
    };

}

std::unique_ptr<PluginInstance> loadVst3(const PluginInfo& info)
{
    if (info.path.empty())
    {
        logPluginLoadFailure(info, "Stored module path is empty");
        return nullptr;
    }

    const auto resolvedModulePath = resolveVst3ModulePath(info.path);
    if (! resolvedModulePath)
    {
        logPluginLoadFailure(info,
                             "Unable to locate a VST3 module near " + pathToString(info.path));
        return nullptr;
    }

    SharedLibrary module;
    if (! module.load(*resolvedModulePath))
    {
        juce::String reason = module.getLastError();
        if (reason.isEmpty())
            reason = "Module load failed";
        logPluginLoadFailure(info,
                             "Unable to load '" + pathToString(*resolvedModulePath) + "': " + reason);
        return nullptr;
    }

    using GetFactoryProc = Steinberg::IPluginFactory*(*)();
    auto* symbol = module.getSymbol("GetPluginFactory");
    if (! symbol)
    {
        logPluginLoadFailure(info, "Module does not export GetPluginFactory");
        return nullptr;
    }

    auto* factory = reinterpret_cast<GetFactoryProc>(symbol)();
    if (! factory)
    {
        logPluginLoadFailure(info, "GetPluginFactory returned nullptr");
        return nullptr;
    }

    Steinberg::FUID requestedClassId;
    const bool hasRequestedId = ! info.id.empty() && requestedClassId.fromString(info.id.c_str());

    Steinberg::PClassInfo classInfo {};
    const auto categorySupported = [](const char* category) -> bool
    {
        if (category == nullptr)
            return false;

        return std::strcmp(category, kVstAudioEffectClass) == 0
               || std::strstr(category, "Instrument") != nullptr;
    };
    Steinberg::Vst::IComponent* component = nullptr;
    Steinberg::Vst::IAudioProcessor* processor = nullptr;
    Steinberg::Vst::IEditController* controller = nullptr;

    auto hostContext = Steinberg::IPtr<SimpleHostApplication>(new SimpleHostApplication());
    auto componentHandler = Steinberg::IPtr<SimpleComponentHandler>(new SimpleComponentHandler());
    bool controllerInitialized = false;
    bool connectionPointsConnected = false;
    auto releaseController = [&](bool terminate)
    {
        if (connectionPointsConnected && component && controller)
        {
            Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection(component);
            Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> controllerConnection(controller);
            if (componentConnection && controllerConnection)
            {
                componentConnection->disconnect(controllerConnection);
                controllerConnection->disconnect(componentConnection);
            }
            connectionPointsConnected = false;
        }

        if (controller)
        {
            if (terminate)
                controller->terminate();
            controller->setComponentHandler(nullptr);
            controller->release();
            controller = nullptr;
        }
        controllerInitialized = false;
        connectionPointsConnected = false;
    };

    bool componentInitialised = false;
    juce::String selectedClassName;
    juce::String lastFailureReason;
    juce::String lastClassName;
    bool sawSupportedCategory = false;
    bool sawRequestedClass = false;
    bool requestedClassFilteredByCategory = false;
    const auto count = factory->countClasses();
    for (Steinberg::int32 i = 0; i < count; ++i)
    {
        if (factory->getClassInfo(i, &classInfo) != Steinberg::kResultOk)
            continue;

        Steinberg::FUID currentId(classInfo.cid);
        const bool matchesRequestedId = hasRequestedId && (currentId == requestedClassId);
        const bool matchesRequestedName = hasRequestedId && !matchesRequestedId && (juce::String(classInfo.name) == juce::String(info.name));

        if (matchesRequestedId || matchesRequestedName)
            sawRequestedClass = true;

        if (matchesRequestedName)
        {
             juce::Logger::writeToLog("Requested ID not found, but found class with matching name: " + juce::String(classInfo.name) + ". Using this class.");
        }

        if (! categorySupported(classInfo.category))
        {
            if (matchesRequestedId || matchesRequestedName)
                requestedClassFilteredByCategory = true;
            continue;
        }

        sawSupportedCategory = true;

        if (hasRequestedId && ! matchesRequestedId && ! matchesRequestedName)
            continue;

        controller = nullptr;
        component = nullptr;
        processor = nullptr;

        juce::String className(classInfo.name);
        auto instantiateWithContext = [&](bool provideHostApplication) -> bool
        {
            Steinberg::Vst::IComponent* newComponent = nullptr;
            const auto createResult = factory->createInstance(classInfo.cid,
                                                              Steinberg::Vst::IComponent::iid,
                                                              reinterpret_cast<void**>(&newComponent));
            if (createResult != Steinberg::kResultOk || newComponent == nullptr)
            {
                lastFailureReason = juce::String("createInstance failed (result ")
                                    + juce::String(static_cast<int>(createResult)) + ")";
                lastClassName = className;
                return false;
            }

            Steinberg::Vst::IAudioProcessor* newProcessor = nullptr;
            const auto processorResult = newComponent->queryInterface(Steinberg::Vst::IAudioProcessor::iid,
                                                                      reinterpret_cast<void**>(&newProcessor));
            if (processorResult != Steinberg::kResultOk || newProcessor == nullptr)
            {
                lastFailureReason = "Component does not expose IAudioProcessor";
                lastClassName = className;
                newComponent->release();
                return false;
            }

            const auto initResult = newComponent->initialize(provideHostApplication && hostContext
                                                                ? static_cast<Steinberg::Vst::IHostApplication*>(hostContext.get())
                                                                : nullptr);
            if (initResult != Steinberg::kResultOk)
            {
                lastFailureReason = juce::String("initialize failed (result ")
                                    + juce::String(static_cast<int>(initResult)) + ")";
                lastClassName = className;
                newProcessor->release();
                newComponent->release();
                return false;
            }

            component = newComponent;
            processor = newProcessor;
            return true;
        };

        if (! instantiateWithContext(true))
        {
            if (! instantiateWithContext(false))
                continue;
        }

        Steinberg::TUID controllerClassId {};
        if (component->getControllerClassId(controllerClassId) == Steinberg::kResultOk)
        {
            if (factory->createInstance(controllerClassId,
                                        Steinberg::Vst::IEditController::iid,
                                        reinterpret_cast<void**>(&controller)) != Steinberg::kResultOk)
            {
                controller = nullptr;
            }
        }

        if (! controller)
        {
            if (component->queryInterface(Steinberg::Vst::IEditController::iid, reinterpret_cast<void**>(&controller)) != Steinberg::kResultOk)
                controller = nullptr;
        }

        selectedClassName = className;
        componentInitialised = true;
        break;
    }

    if (! componentInitialised || ! component || ! processor)
    {
        juce::String failureMessage;
        if (hasRequestedId && ! sawRequestedClass)
        {
            failureMessage = "Requested class id " + juce::String(info.id) + " was not reported by the module. Available classes:";
            for (Steinberg::int32 i = 0; i < count; ++i)
            {
                if (factory->getClassInfo(i, &classInfo) == Steinberg::kResultOk)
                {
                    Steinberg::FUID currentId(classInfo.cid);
                    char uidString[33];
                    currentId.toString(uidString);
                    failureMessage += "\n  - " + juce::String(classInfo.name) + " (" + juce::String(uidString) + ")";
                }
            }
        }
        else if (hasRequestedId && requestedClassFilteredByCategory)
            failureMessage = "Requested class id " + juce::String(info.id) + " is not an audio effect or instrument";
        else if (! sawSupportedCategory)
            failureMessage = "Factory reported no audio effect or instrument classes";
        else if (lastFailureReason.isNotEmpty())
        {
            const juce::String name = lastClassName.isNotEmpty() ? lastClassName : juce::String("component");
            failureMessage = "Failed to instantiate '" + name + "': " + lastFailureReason;
        }
        else
            failureMessage = "No compatible classes could be instantiated";

        factory->release();
        logPluginLoadFailure(info, failureMessage);
        return nullptr;
    }

    factory->release();

    if (! controller && component)
    {
        if (component->queryInterface(Steinberg::Vst::IEditController::iid, reinterpret_cast<void**>(&controller)) != Steinberg::kResultOk)
            controller = nullptr;
    }

    if (controller)
    {
        controller->setComponentHandler(componentHandler);
        auto initializeController = [&](Steinberg::Vst::IHostApplication* hostApp) -> bool
        {
            return controller->initialize(hostApp) == Steinberg::kResultOk;
        };

        bool initializedWithHost = false;
        if (hostContext)
            initializedWithHost = initializeController(static_cast<Steinberg::Vst::IHostApplication*>(hostContext.get()));

        if (initializedWithHost || initializeController(nullptr))
            controllerInitialized = true;
        else
            releaseController(false);
    }

    if (controller && controllerInitialized && component)
    {
        auto stateStream = Steinberg::owned(new Steinberg::MemoryStream());
        if (stateStream)
        {
            const auto stateResult = component->getState(stateStream);
            if (stateResult == Steinberg::kResultOk || stateResult == Steinberg::kResultTrue)
            {
                stateStream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                controller->setComponentState(stateStream);
            }
        }
    }

    if (controller && controllerInitialized && component && ! connectionPointsConnected)
    {
        Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection(component);
        Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> controllerConnection(controller);
        if (componentConnection && controllerConnection)
        {
            const auto componentToController = componentConnection->connect(controllerConnection);
            const auto controllerToComponent = controllerConnection->connect(componentConnection);
            if (componentToController == Steinberg::kResultOk && controllerToComponent == Steinberg::kResultOk)
                connectionPointsConnected = true;
            else
            {
                componentConnection->disconnect(controllerConnection);
                controllerConnection->disconnect(componentConnection);
            }
        }
    }

    const auto inputBusCount = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    const auto outputBusCount = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    const juce::String classLabel = selectedClassName.isNotEmpty() ? ("'" + selectedClassName + "'")
                                                                   : juce::String("component");

    if (outputBusCount <= 0)
    {
        logPluginLoadFailure(info,
                             "Component " + classLabel + " exposes no audio output buses");
        component->terminate();
        processor->release();
        component->release();
        releaseController(controllerInitialized);
        return nullptr;
    }

    Steinberg::Vst::BusInfo inBus {};
    Steinberg::Vst::BusInfo outBus {};
    Steinberg::Vst::SpeakerArrangement inputArrangement = Steinberg::Vst::SpeakerArr::kEmpty;
    Steinberg::Vst::SpeakerArrangement outputArrangement = Steinberg::Vst::SpeakerArr::kStereo;

    int inputChannels = 0;
    if (inputBusCount > 0)
    {
        if (component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, inBus) != Steinberg::kResultOk)
        {
            logPluginLoadFailure(info,
                                 "Failed to query audio input bus info for " + classLabel);
            component->terminate();
            processor->release();
            component->release();
            releaseController(controllerInitialized);
            return nullptr;
        }

        inputChannels = static_cast<int>(inBus.channelCount);
        component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
        processor->getBusArrangement(Steinberg::Vst::kInput, 0, inputArrangement);
        if (inputArrangement == Steinberg::Vst::SpeakerArr::kEmpty)
        {
            if (inputChannels == 1)
                inputArrangement = Steinberg::Vst::SpeakerArr::kMono;
            else if (inputChannels == 2)
                inputArrangement = Steinberg::Vst::SpeakerArr::kStereo;
        }
    }

    const auto outBusInfoResult = component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, outBus);
    if (outBusInfoResult != Steinberg::kResultOk || outBus.channelCount <= 0)
    {
        const juce::String reason = outBusInfoResult != Steinberg::kResultOk
                                        ? juce::String("Failed to query audio output bus info for ")
                                        : juce::String("Audio output bus reports zero channels for ");
        logPluginLoadFailure(info, reason + classLabel);
        component->terminate();
        processor->release();
        component->release();
        releaseController(controllerInitialized);
        return nullptr;
    }

    const int outputChannels = static_cast<int>(outBus.channelCount);
    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
    processor->getBusArrangement(Steinberg::Vst::kOutput, 0, outputArrangement);

    if (outputArrangement == Steinberg::Vst::SpeakerArr::kEmpty && outputChannels == 2)
        outputArrangement = Steinberg::Vst::SpeakerArr::kStereo;
    else if (outputArrangement == Steinberg::Vst::SpeakerArr::kEmpty && outputChannels == 1)
        outputArrangement = Steinberg::Vst::SpeakerArr::kMono;

    Steinberg::Vst::SpeakerArrangement desiredInputArrangement = inputArrangement;
    Steinberg::Vst::SpeakerArrangement desiredOutputArrangement = outputArrangement;
    const Steinberg::int32 desiredInputBusCount = inputBusCount > 0 ? 1 : 0;
    const auto setArrangementResult = processor->setBusArrangements(desiredInputBusCount > 0 ? &desiredInputArrangement : nullptr,
                                                                    desiredInputBusCount,
                                                                    &desiredOutputArrangement,
                                                                    1);
    if (setArrangementResult == Steinberg::kResultOk)
    {
        if (desiredInputBusCount > 0)
            inputArrangement = desiredInputArrangement;
        outputArrangement = desiredOutputArrangement;
    }

    return std::make_unique<Vst3PluginInstance>(std::move(module),
                                                component,
                                                processor,
                                                controller,
                                                inputChannels,
                                                outputChannels,
                                                inputArrangement,
                                                outputArrangement,
                                                true,
                                                std::move(hostContext),
                                                std::move(componentHandler),
                                                controllerInitialized,
                                                connectionPointsConnected);
}

} // namespace host::plugin
