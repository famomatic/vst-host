# VST Host Scaffold

A Windows x64 JUCE application that hosts audio plug-ins with a node-based
processing graph. The scaffold targets MSVC 2022 with C++20, sets up the
Steinberg VST3 SDK, and provides a modular architecture for building a
full-featured effect host.

## Highlights

- **Graph-driven engine** – `host::graph::GraphEngine` schedules nodes for each
  block, handling audio/MIDI routing and dynamic graph edits.
- **Device abstraction** – `host::audio::DeviceEngine` wraps JUCE's
  `AudioDeviceManager`, manages block-size/sample-rate conversions, and bridges
  hardware I/O to the processing graph through lock-free data paths.
- **Plugin management** – `host::plugin::PluginScanner` discovers VST3 (and
  optional VST2) plug-ins, caches metadata to JSON, and feeds the runtime graph
  when users instantiate plug-ins.
- **Persistence layer** – `host::persist` components serialize configuration,
  plug-in graphs, and presets to disk for crash-safe restores.
- **GUI shell** – `gui::MainWindow` presents a split layout with a plug-in
  browser, draggable graph view, preferences dialogs, and a system tray
  integration for quick access to device settings.
- **Localization helpers** – `util::Localization` loads translation bundles and
  exposes the `host::i18n::tr()` helper used throughout the UI.

## Repository layout

```
CMakeLists.txt          # Top-level target definitions and JUCE integration
externals/              # Third-party SDKs (checked out manually)
  JUCE/                 # JUCE framework submodule
  VST3_SDK/             # Steinberg VST3 SDK (copy files here)
  vst2sdk/              # Optional Xaymar VST2 headers
resources/              # Sample JSON data, localization seeds, fixtures
src/
  AppMain.cpp           # JUCE application entry point
  audio/                # Device bridging, resampling, and buffer helpers
  graph/                # GraphEngine core, node implementations, schedulers
  gui/                  # Windows, views, and dialogs built with JUCE widgets
  host/                 # Plugin loading, scanning, shared VST3 utilities
  persist/              # Config/project serialization and preset formats
  util/                 # Cross-cutting utilities (localization, helpers)
```

The build outputs live under `build/` (created during configuration). Avoid
committing generated artefacts.

## Prerequisites

1. **Visual Studio 2022** with the *Desktop development with C++* workload.
2. **CMake 3.22+** available in the command prompt (VS ships a compatible
   version).
3. **JUCE** checked out as a submodule:
   ```bash
   git submodule add https://github.com/juce-framework/JUCE.git externals/JUCE
   git submodule update --init --recursive
   ```
4. **Steinberg VST3 SDK** extracted to `externals/VST3_SDK`.
5. (Optional) **VST2 headers** from
   [Xaymar/vst2sdk](https://github.com/Xaymar/vst2sdk) extracted to
   `externals/vst2sdk`.

> Tip: After cloning, run `git submodule update --init --recursive` to populate
> JUCE if the submodule already exists in your fork.

## Building the host

1. Open a **x64 Native Tools Command Prompt for VS 2022**.
2. Configure once:
   ```bash
   cmake -S . -B build -DJUCE_BUILD_EXAMPLES=OFF
   ```
3. Build the desktop host (Debug for development, Release for packaged builds):
   ```bash
   cmake --build build --config Debug --target VSTHostApp
   cmake --build build --config Release --target VSTHostApp
   ```
4. Launch the resulting binary:
   ```
   build/src/VSTHostApp_artefacts/Debug/VST Host/VSTHostApp.exe
   ```

Subsequent iterations only require step 3. When adding new plug-in SDKs or
changing JUCE modules, rerun the configure step to regenerate project files.

## Development workflow

- **IDE integration** – Use `cmake --build build --config Debug --target ALL_BUILD`
  to keep Visual Studio project files updated. The generated solution resides in
  `build/`.
- **Realtime safety** – Audio callbacks live on the realtime thread. Avoid
  dynamic allocations, locks, or logging from `DeviceEngine::audioDeviceIOCallbackWithContext`.
- **Adding graph nodes** – Implement new subclasses of `host::graph::Node` under
  `src/graph/Nodes/`, register them in `graph::NodeFactory`, and expose UI hooks
  via `gui::GraphView`.
- **Plugin scanning** – Extend `PluginScanner::getDefaultSearchPaths()` to add
  vendor-specific directories. Cached results are stored beside the executable
  under `%APPDATA%/VST Host/` (Windows) and mirrored in `resources/` for tests.
- **Configuration** – `persist::Config` reads/writes a JSON blob containing audio
  device choices, localization settings, and plug-in search paths. Update the
  schema version when fields change.

## Testing

Automated tests are not yet present. When introducing tests, prefer JUCE's
`UnitTest` harness or integrate GoogleTest via CMake and register suites with
`add_test(...)` so `ctest --test-dir build --output-on-failure` can execute them.
For manual verification, launch the host, pick an ASIO or WASAPI device, scan
for plug-ins, instantiate a few nodes, and confirm audio passes through the
graph without glitches.

## Resources and localization

- `resources/sample-project.json` – Minimal project file illustrating how nodes
  and connections are serialized.
- `resources/sample-plugin-cache.json` – Example plug-in cache written by the
  scanner.
- `util::Localization` looks for translation bundles under `%APPDATA%/VST Host/`
  at runtime. Seed files in `resources/i18n` (add as needed) to bootstrap new
  locales.

## Troubleshooting

- **Missing SDK includes** – Ensure the VST3 SDK directory is named exactly
  `externals/VST3_SDK` and contains the `pluginterfaces/` headers. CMake will
  abort if the path is not found.
- **Linker errors for JUCE modules** – Verify the JUCE submodule is checked out
  and that `git submodule update --init --recursive` completed without errors.
- **Audio device failures** – Use the preferences dialog or the tray icon to
  select a different driver. The device engine falls back to silence when the
  driver reports zero channels.

## Contributing

Follow the guidance in `AGENTS.md` for coding conventions, commit structure, and
pull request expectations. Contributions that touch plug-in loading or device
handling should call out the risk in the PR summary so reviewers can schedule
thorough regressions.
