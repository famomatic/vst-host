# VST Host Scaffold

This repository provides a Windows x64 C++20 scaffold for a modular audio-effect VST host built with JUCE and the Steinberg VST3 SDK. It targets MSVC 2022 with CMake 3.22 or newer and sets up the foundation for plugin scanning, graph-based audio routing, and project persistence.

## Directory layout

```
externals/
  JUCE/           # JUCE submodule (add with git submodule add)
  VST3_SDK/       # Steinberg VST3 SDK extracted here
  vst2sdk/        # Optional Xaymar VST2 clean-room headers
resources/        # Sample JSON data and future assets
src/              # Application sources (audio engine, host wrappers, GUI, persistence)
```

## Prerequisites

* **Visual Studio 2022** with the Desktop development with C++ workload.
* **CMake 3.22+** on PATH (bundled with VS 2022).
* **JUCE** checked out as a submodule under `externals/JUCE`:
  ```bash
  git submodule add https://github.com/juce-framework/JUCE.git externals/JUCE
  git submodule update --init --recursive
  ```
* **Steinberg VST3 SDK** extracted to `externals/VST3_SDK`.
* Optional **VST2 headers** from [Xaymar/vst2sdk](https://github.com/Xaymar/vst2sdk) extracted to `externals/vst2sdk`.

## Building

1. Open a **x64 Native Tools Command Prompt for VS 2022**.
2. Configure with CMake:
   ```bash
   cmake -S . -B build -DJUCE_BUILD_EXAMPLES=OFF
   ```
3. Build the host:
   ```bash
   cmake --build build --config Release
   ```
4. The executable `VSTHostApp.exe` is produced under `build/Release/`.

## Runtime notes

* The host prefers **ASIO** drivers and falls back to **WASAPI Exclusive** via JUCE's device selector.
* Engine processing runs at a configurable sample rate/block size, with device I/O resampled via `juce::LagrangeInterpolator`.
* Plugins are scanned from user-managed directories. Results are cached to JSON (see `resources/sample-plugin-cache.json`).
* Projects serialize the processing graph and plugin metadata (`resources/sample-project.json`).

## Next steps

* Implement full plugin instantiation (VST3/VST2) and state capture.
* Replace the placeholder resampler with r8brain-free for higher fidelity.
* Expand the graph UI to support drag-and-drop placement and connection editing.
* Add background workers for crash-safe plugin scanning and preset loading.
