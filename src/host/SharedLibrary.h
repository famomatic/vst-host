#pragma once

#include <filesystem>
#include <string>
#include <juce_core/juce_core.h>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace host::plugin
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
}
