# Repository Guidelines

## Architecture overview

- **Audio engine (`src/audio/`)** – Handles real-time device callbacks,
  resampling, and conversion between hardware buffers and the processing graph.
  Public entry points must remain lock-free once initialised.
- **Graph runtime (`src/graph/`)** – Owns the `GraphEngine`, node definitions,
  and scheduling logic. Any new node must inherit from `host::graph::Node`,
  implement `prepare`, `process`, and expose metadata for persistence.
- **GUI (`src/gui/`)** – Contains JUCE components (main window, graph view,
  plugin browser, preferences). UI classes interact with background services via
  message thread-safe queues or async callbacks.
- **Host layer (`src/host/`)** – Wraps VST3/VST2 SDK calls (plugin hosting,
  scanning, factory helpers). Keep SDK usage isolated here to avoid leaking
  externals into other modules.
- **Persistence (`src/persist/`)** – Reads/writes JSON configuration, project, and
  preset data. Update schema versions when changing saved structures and provide
  migration logic.
- **Utilities (`src/util/`)** – Shared helpers such as localization utilities.
  Keep this lightweight and platform-agnostic.
- **Resources (`resources/`)** – Sample data and seeds for caches, configs, and
  localization. Treat these files as fixtures for development and tests.

## Build, test, and development commands

1. Configure CMake once per build directory:
   ```bash
   cmake -S . -B build -DJUCE_BUILD_EXAMPLES=OFF
   ```
2. Build during iteration:
   ```bash
   cmake --build build --config Debug --target VSTHostApp
   ```
3. Produce a Release artefact when preparing binaries:
   ```bash
   cmake --build build --config Release --target VSTHostApp
   ```
4. If tests are added, expose them via `add_test` and execute with:
   ```bash
   ctest --test-dir build --output-on-failure
   ```
5. Generated binaries live under
   `build/src/VSTHostApp_artefacts/<Config>/VST Host/`.

## Coding style & conventions

- Target **C++20**; prefer standard library facilities before introducing
  dependencies.
- Use **four-space indentation** and **brace-on-same-line** style (see
  `src/host/PluginHost.cpp` for reference).
- Naming:
  - Types and classes – `PascalCase`
  - Functions, variables – `camelCase`
  - Constants – `kCapitalized`
- Group includes: standard library, third-party SDKs, then project headers.
  Place `#define NOMINMAX` before including `windows.h`.
- Favor RAII and smart pointers over manual `new`/`delete`. Avoid raw pointers in
  ownership positions.
- Maintain realtime safety in audio callbacks: no allocations, locking, or
  blocking I/O.
- Keep GUI actions on the JUCE message thread; move long-running work to
  background threads via `juce::ThreadPool` or `juce::Thread`.
- Document non-trivial behaviour with concise comments. Update the README when
  module-level behaviour changes.

## Module-specific guidance

- **Graph nodes** – Register new node types with the factory and provide
  serialization hooks so projects/presets remain round-trippable.
- **Plugin scanning** – Extend default search paths cautiously. Any filesystem or
  registry queries must handle missing keys gracefully to avoid blocking the UI.
- **Persistence** – When adding new fields, supply default initialisers and write
  migration steps for legacy project files.
- **Localization** – Add new translation keys to the base language file first and
  keep keys stable; changing keys requires updating all locales.

## Testing expectations

- New features that touch audio streaming, graph scheduling, or plugin lifecycle
  should include either automated coverage or detailed manual test notes.
- Smoke test the application after modifying device or plugin handling:
  1. Launch the host.
  2. Select an ASIO or WASAPI device.
  3. Scan plug-ins and instantiate at least one VST3 effect.
  4. Confirm audio passes and UI remains responsive.

## Git & documentation

- Follow short imperative commit messages (≤72 characters), e.g.
  `Implement graph connection inspector`.
- Group related changes together; avoid mixing refactors with behavioural
  changes unless necessary.
- Pull requests must include:
  1. Summary of changes and rationale.
  2. Testing commands with results.
  3. Linked issues or TODO references where applicable.
  4. Screenshots/GIFs for UI-impacting updates.
  5. Risk callouts when touching device handling or plugin loading.
- Update `README.md` or relevant docs whenever the external workflow or module
  boundaries shift.
