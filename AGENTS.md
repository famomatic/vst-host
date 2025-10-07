# Repository Guidelines

## Project Structure & Module Organization
Sources live in `src/`, split by responsibility: `audio/` for the engine primitives, `graph/` for routing logic, `gui/` for JUCE windows, `host/` for plugin wrappers, and `persist/` for project serialization. External SDKs are vendored in `externals/` (JUCE, Steinberg VST3, optional VST2 headers). Runtime fixtures such as sample project JSON sit under `resources/`. Out-of-tree builds land in `build/`; avoid editing files there directly.

## Build, Test, and Development Commands
Configure once: `cmake -S . -B build -DJUCE_BUILD_EXAMPLES=OFF`. Build Release binaries with `cmake --build build --config Release`, or Debug when profiling. After a build, the Windows executable is under `build/src/VSTHostApp_artefacts/VST Host`. If you add CTest targets, run them with `ctest --test-dir build --output-on-failure`. During iteration, `cmake --build build --config Debug --target VSTHostApp` recompiles the host only.

## Coding Style & Naming Conventions
Code targets C++20 and prefers standard library utilities (`std::array`, `std::clamp`, `std::optional`). Use four-space indentation and place braces on the same line as class or control statements, mirroring existing files like `src/host/PluginHost.cpp`. Name types in `PascalCase`, functions and variables in `camelCase`, and constants in `kCapitalized`. Keep `#include` blocks grouped (standard, externals, project) and add `#define NOMINMAX` before including `windows.h` to avoid macro collisions. Favor RAII and smart pointers over raw new/delete.

## Testing Guidelines
No first-party automated tests ship yet. When introducing tests, prefer JUCE's `UnitTest` harness or GoogleTest integrated via CMake, and expose them to CTest with `add_test`. Name test files `<Component>Tests.cpp` and keep them under a `tests/` directory inside the relevant module. On Windows builds, complement automated runs with a smoke test: launch `VST Host`, load a VST3 effect, and verify audio passes without glitches.

## Commit & Pull Request Guidelines
Follow the existing short imperative commit style (`Implement graph connection inspector`, `Fix Windows min/max macro collision`). Group logical changes together and keep subjects under ~72 characters; add context in the body when behavior changes. For pull requests, include: 1) summary of changes and rationale, 2) testing notes with commands run, 3) linked issues or TODO references, and 4) screenshots or GIFs when UI is affected. Label risky changes that touch device handling or plugin loading so reviewers can prioritize full regression passes.
