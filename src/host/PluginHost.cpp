#include "host/PluginHost.h"

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
#include <pluginterfaces/vst/ivstcontextmenu.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vsttypes.h>

#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>

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
    class SimpleHostApplication final : public Steinberg::Vst::IHostApplication
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

#if defined(_WIN32)
    using ModuleHandle = HMODULE;
#else
    using ModuleHandle = void*;
#endif

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

    [[nodiscard]] bool isCandidateModuleFile(const std::filesystem::path& path)
    {
        auto ext = toLowerCopy(path.extension().string());
        if (ext == ".vst3" || ext == ".dll" || ext == ".so" || ext == ".dylib")
            return true;

        if (ext.empty() && isLikelyMacOsBinary(path))
            return true;

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
            if (isCandidateModuleFile(candidate))
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
            if (isCandidateModuleFile(providedPath))
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

        addSearchRoot(providedPath);

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

        for (const auto& root : searchRoots)
        {
            if (const auto found = findModuleInDirectory(root))
                return found;
        }

        return std::nullopt;
    }

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
            lastError.clear();
            handle = ::LoadLibraryW(p.wstring().c_str());
            if (! handle)
            {
                const DWORD errorCode = ::GetLastError();
                if (errorCode != 0)
                {
                    LPWSTR buffer = nullptr;
                    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
                    const DWORD length = ::FormatMessageW(flags,
                                                          nullptr,
                                                          errorCode,
                                                          0,
                                                          reinterpret_cast<LPWSTR>(&buffer),
                                                          0,
                                                          nullptr);

                    if (length > 0 && buffer != nullptr)
                    {
                        lastError = juce::String(buffer).trim();
                        ::LocalFree(buffer);
                    }
                    else
                    {
                        lastError = juce::String("LoadLibrary failed with error ") + juce::String(static_cast<int>(errorCode));
                    }
                }
                else
                {
                    lastError = "LoadLibrary failed";
                }
            }
#else
            lastError.clear();
            ::dlerror();
            handle = ::dlopen(p.string().c_str(), RTLD_NOW);
            if (! handle)
            {
                if (const char* err = ::dlerror())
                    lastError = juce::String(err).trim();
                else
                    lastError = "dlopen failed";
            }
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
            lastError.clear();
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
        [[nodiscard]] const juce::String& getLastError() const noexcept { return lastError; }

    private:
        ModuleHandle handle {};
        juce::String lastError;
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

    class Vst3EditorComponent final : public juce::Component,
                                      public Steinberg::IPlugFrame
    {
    public:
        explicit Vst3EditorComponent(Vst3PluginInstance& ownerIn);
        ~Vst3EditorComponent() override;

        [[nodiscard]] bool isViewAvailable() const noexcept { return view != nullptr; }

        void parentHierarchyChanged() override;
        void visibilityChanged() override;
        void focusGained(juce::Component::FocusChangeType cause) override;
        void focusLost(juce::Component::FocusChangeType cause) override;
        void resized() override;

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* requestedView, Steinberg::ViewRect* newSize) override;

    private:
        void ensureView();
        void adjustToInitialSize();
        void attachIfPossible();
        void detachView();
        void releaseView();
        void updateScaleFactor();

        Vst3PluginInstance& owner;
        Steinberg::IPlugView* view { nullptr };
        Steinberg::IPtr<Steinberg::IPlugViewContentScaleSupport> scaleSupport;
        bool attached { false };
    };

    class Vst3PluginInstance final : public PluginInstance
    {
    public:
        static constexpr int kMaxChannels = 8;

        friend class Vst3EditorComponent;

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

        std::unique_ptr<juce::Component> createEditorComponent() override
        {
            auto editor = std::make_unique<Vst3EditorComponent>(*this);
            if (! editor->isViewAvailable())
                return {};
            return editor;
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

            if (component->initialize(hostContext ? hostContext.get() : nullptr) == Steinberg::kResultOk)
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

            return controller->createView(Steinberg::Vst::ViewType::kEditor);
        }

        void releaseEditorView(Steinberg::IPlugView* viewToRelease)
        {
            if (viewToRelease != nullptr)
                viewToRelease->release();
        }
    };

    constexpr int kVst2MaxChannels = 32;

    Vst3EditorComponent::Vst3EditorComponent(Vst3PluginInstance& ownerIn)
        : owner(ownerIn)
    {
        setOpaque(false);
        ensureView();
        adjustToInitialSize();
    }

    Vst3EditorComponent::~Vst3EditorComponent()
    {
        detachView();
        releaseView();
    }

    void Vst3EditorComponent::parentHierarchyChanged()
    {
        juce::Component::parentHierarchyChanged();
        attachIfPossible();
    }

    void Vst3EditorComponent::visibilityChanged()
    {
        juce::Component::visibilityChanged();
        if (isShowing())
            attachIfPossible();
        else
            detachView();
    }

    void Vst3EditorComponent::focusGained(juce::Component::FocusChangeType cause)
    {
        juce::Component::focusGained(cause);
        if (view)
            view->onFocus(true);
    }

    void Vst3EditorComponent::focusLost(juce::Component::FocusChangeType cause)
    {
        juce::Component::focusLost(cause);
        if (view)
            view->onFocus(false);
    }

    void Vst3EditorComponent::resized()
    {
        juce::Component::resized();
        if (view && attached)
        {
            Steinberg::ViewRect rect { 0, 0, getWidth(), getHeight() };
            view->onSize(&rect);
        }
    }

    Steinberg::tresult PLUGIN_API Vst3EditorComponent::queryInterface(const Steinberg::TUID iid, void** obj)
    {
        if (obj == nullptr)
            return Steinberg::kInvalidArgument;

        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)
            || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid))
        {
            *obj = static_cast<Steinberg::IPlugFrame*>(this);
            addRef();
            return Steinberg::kResultOk;
        }

        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::tresult PLUGIN_API Vst3EditorComponent::resizeView(Steinberg::IPlugView* requestedView, Steinberg::ViewRect* newSize)
    {
        if (requestedView != view || newSize == nullptr)
            return Steinberg::kInvalidArgument;

        const int width = newSize->right - newSize->left;
        const int height = newSize->bottom - newSize->top;

        juce::Component::SafePointer<Vst3EditorComponent> safeComponent(this);
        Steinberg::IPtr<Steinberg::IPlugView> retained(requestedView);
        juce::MessageManager::callAsync([safeComponent, width, height, retained]()
        {
            if (auto* comp = safeComponent.getComponent())
            {
                comp->setSize(width, height);
                Steinberg::ViewRect rect { 0, 0, width, height };
                retained->onSize(&rect);
            }
        });

        return Steinberg::kResultOk;
    }

    void Vst3EditorComponent::ensureView()
    {
        if (view)
            return;

        view = owner.createEditorView();
        if (! view)
            return;

        Steinberg::IPlugViewContentScaleSupport* support = nullptr;
        if (view->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid, reinterpret_cast<void**>(&support)) == Steinberg::kResultOk && support != nullptr)
            scaleSupport = Steinberg::IPtr<Steinberg::IPlugViewContentScaleSupport>::adopt(support);

        adjustToInitialSize();
    }

    void Vst3EditorComponent::adjustToInitialSize()
    {
        if (! view)
            return;

        Steinberg::ViewRect rect {};
        if (view->getSize(&rect) == Steinberg::kResultOk)
        {
            const int initialWidth = rect.right - rect.left;
            const int initialHeight = rect.bottom - rect.top;
            if (initialWidth > 0 && initialHeight > 0)
                setSize(initialWidth, initialHeight);
        }
    }

    void Vst3EditorComponent::attachIfPossible()
    {
        if (attached || ! isShowing())
            return;

        ensureView();
        if (! view)
            return;

        if (auto* peer = getPeer())
        {
            void* nativeHandle = peer->getNativeHandle();
            if (nativeHandle == nullptr)
                return;

            const char* platformType = nullptr;
           #if JUCE_WINDOWS
            platformType = Steinberg::kPlatformTypeHWND;
           #elif JUCE_MAC
            platformType = Steinberg::kPlatformTypeNSView;
           #else
            platformType = Steinberg::kPlatformTypeX11EmbedWindowID;
           #endif

            if (view->isPlatformTypeSupported(platformType) != Steinberg::kResultTrue)
                return;

            if (view->attached(nativeHandle, platformType) == Steinberg::kResultOk)
            {
                view->setFrame(this);
                attached = true;
                updateScaleFactor();
                Steinberg::ViewRect rect { 0, 0, getWidth(), getHeight() };
                view->onSize(&rect);
            }
        }
    }

    void Vst3EditorComponent::detachView()
    {
        if (! view || ! attached)
            return;

        view->setFrame(nullptr);
        view->removed();
        attached = false;
    }

    void Vst3EditorComponent::releaseView()
    {
        scaleSupport = nullptr;
        if (view)
        {
            owner.releaseEditorView(view);
            view = nullptr;
        }
    }

    void Vst3EditorComponent::updateScaleFactor()
    {
        if (! scaleSupport || ! view)
            return;

        if (auto* peer = getPeer())
        {
            const auto scale = peer->getPlatformScaleFactor();
            scaleSupport->setContentScaleFactor(scale);
        }
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
            static HostTimeInfo timeInfo {};
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
        default:
            break;
        }

        return 0;
    }
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
        if (matchesRequestedId)
            sawRequestedClass = true;

        if (! categorySupported(classInfo.category))
        {
            if (matchesRequestedId)
                requestedClassFilteredByCategory = true;
            continue;
        }

        sawSupportedCategory = true;

        if (hasRequestedId && ! matchesRequestedId)
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
                                                                ? hostContext.get()
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

            hostContext = nullptr;
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

    factory->release();

    if (! componentInitialised || ! component || ! processor)
    {
        juce::String failureMessage;
        if (hasRequestedId && ! sawRequestedClass)
            failureMessage = "Requested class id " + juce::String(info.id) + " was not reported by the module";
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

        logPluginLoadFailure(info, failureMessage);
        return nullptr;
    }

    if (! controller && component)
    {
        if (component->queryInterface(Steinberg::Vst::IEditController::iid, reinterpret_cast<void**>(&controller)) != Steinberg::kResultOk)
            controller = nullptr;
    }

    if (controller)
    {
        controller->setComponentHandler(componentHandler);
        if (controller->initialize(hostContext ? hostContext.get() : nullptr) != Steinberg::kResultOk)
        {
            releaseController(false);
        }
        else
        {
            controllerInitialized = true;
        }
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
