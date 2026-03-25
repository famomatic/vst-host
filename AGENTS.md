# Repository Guidelines

## Response Workflow

Before answering implementation, design, or bug questions:

- Inspect relevant source files first.
- Do not answer based on assumptions when code can be checked in-repo.
- If context is unclear, locate related modules using search (`rg`) and inspect them.

---

## Planning Requirement

Always start non-trivial work by creating and maintaining a **detailed plan**.

The plan acts as a working memory for the session and must remain updated during the task.

A valid plan must include:

- investigation targets (files, modules, or components to inspect)
- the current behavior and a root-cause hypothesis
- the intended change scope
- directly connected files that may also require modification
- exact verification steps
- conditions that would require revising the plan

Generic plans are not acceptable.

Invalid examples:

- inspect code
- apply fix
- run checks

Each step must be concrete enough to execute without guesswork.

Do not begin editing until the plan is specific enough to guide the task.

Plans are **provisional working notes**, not constraints.

If inspection or verification disproves earlier assumptions, revise the plan immediately.

---

## Task Classification

Before making changes, classify the task as one of:

- docs-only
- config-only
- env-only
- code
- cross-cutting

Choose verification based on that classification and avoid unrelated commands.

---

# Architecture Overview

- **Audio engine (`src/audio/`)** - Handles real-time device callbacks, resampling, and conversion between hardware buffers and the processing graph. Public entry points must remain lock-free once initialized.
- **Graph runtime (`src/graph/`)** - Owns the `GraphEngine`, node definitions, and scheduling logic. Any new node must inherit from `host::graph::Node`, implement `prepare`, `process`, and expose metadata for persistence.
- **GUI (`src/gui/`)** - Contains JUCE components (main window, graph view, plugin browser, preferences). UI classes interact with background services via message thread-safe queues or async callbacks.
- **Host layer (`src/host/`)** - Wraps VST3/VST2 SDK calls (plugin hosting, scanning, factory helpers). Keep SDK usage isolated here to avoid leaking externals into other modules.
- **Persistence (`src/persist/`)** - Reads/writes JSON configuration, project, and preset data. Update schema versions when changing saved structures and provide migration logic.
- **Utilities (`src/util/`)** - Shared helpers such as localization utilities. Keep this lightweight and platform-agnostic.
- **Resources (`resources/`)** - Sample data and seeds for caches, configs, and localization. Treat these files as fixtures for development and tests.

---

# Scope & Change Control

## Scope Detection

Before making code changes, determine the minimal scope required.

Classify the scope as one of:

- single function
- single file
- module
- cross-module

Prefer the smallest scope that fully solves the task.

If build, test, or runtime verification reveals directly connected breakage, expand scope only as much as required to resolve it.

Files required to resolve compile errors, type errors, link failures, or runtime integration problems are considered related files.

Avoid modifying files that are clearly unrelated to the request.

---

## Patch Minimalism

Changes must remain strictly related to the requested task.

Avoid:

- refactoring unrelated code
- renaming unrelated symbols
- reformatting untouched code
- architectural rewrites
- introducing new abstractions without necessity

Every changed line must have a clear relationship to the task.

However, if verification reveals breakage in directly connected code, fix only what is required to restore correct behavior.

---

## Defensive Code Policy

Avoid speculative defensive programming.

Do not introduce layered guards such as repeated `if` chains or excessive `try/catch` blocks unless they address a known failure mode.

A single direct guard or error check is acceptable when:

- preventing a concrete bug
- handling external input or API responses
- matching patterns already used in the codebase

The goal is to avoid defensive noise, not to prohibit legitimate safeguards.

---

## Request Handling

Do not refuse user requests unless the change would clearly:

- break the build
- corrupt data
- introduce a security vulnerability
- violate repository rules

If the request is technically feasible and does not harm system stability, it should be implemented.

---

# Build, Test, and Development Commands

1. Configure CMake once per build directory:

   ```bash
   cmake -S . -B build -DJUCE_BUILD_EXAMPLES=OFF
   ```

2. Build during iteration:

   ```bash
   cmake --build build --config Debug --target VSTHostApp
   ```

3. Produce a Release artifact when preparing binaries:

   ```bash
   cmake --build build --config Release --target VSTHostApp
   ```

4. If tests are added, expose them via `add_test` and execute with:

   ```bash
   ctest --test-dir build --output-on-failure
   ```

5. Generated binaries live under `build/src/VSTHostApp_artefacts/<Config>/VST Host/`.

---

# Coding Style & Conventions

- Target **C++20**; prefer standard library facilities before introducing dependencies.
- Use **four-space indentation** and **brace-on-same-line** style (see `src/host/PluginHost.cpp` for reference).
- Naming:
  - Types and classes: `PascalCase`
  - Functions and variables: `camelCase`
  - Constants: `kCapitalized`
- Group includes in order: standard library, third-party SDKs, then project headers.
- Place `#define NOMINMAX` before including `windows.h`.
- Favor RAII and smart pointers over manual `new`/`delete`.
- Avoid raw pointers in ownership positions.
- Maintain realtime safety in audio callbacks: no allocations, locking, or blocking I/O.
- Keep GUI actions on the JUCE message thread; move long-running work to background threads (`juce::ThreadPool` or `juce::Thread`).
- Document non-trivial behavior with concise comments.
- Update README when module-level behavior changes.

---

# Module-Specific Guidance

- **Graph nodes**: Register new node types with the factory and provide serialization hooks so projects/presets remain round-trippable.
- **Plugin scanning**: Extend default search paths cautiously. Filesystem or registry queries must handle missing keys gracefully to avoid blocking the UI.
- **Persistence**: When adding new fields, supply default initializers and write migration steps for legacy project files.
- **Localization**: Add new translation keys to the base language file first and keep keys stable.

---

# Testing & Verification

Validation must remain scoped to the actual diff.

Do not run commands unrelated to modified code.

## Diff-aware verification

Examples:

- documentation change -> formatting/spell check only
- comment change -> no build
- small logic change -> targeted build and focused tests
- build-system change -> reconfigure and build affected target

If unsure whether a change affects compile or runtime behavior, assume that it does.

## Verification command order

When verification commands are required, run in this order:

1. formatting for touched files (if formatter is configured)
2. `cmake --build build --config Debug --target VSTHostApp`
3. `ctest --test-dir build --output-on-failure` (when tests are present and affected)

Rules:

- Stop if any earlier step fails.
- Do not run later steps if earlier validation fails.
- Prefer the smallest affected scope when tooling supports it.

## Minimum required verification by change type

### Documentation-only changes (`*.md`, comments)

- Do not run full build unless docs alter generated code or tooling behavior.
- Run formatting only for modified docs if needed.

### Config-only changes

Examples:

- CMake configuration
- CI configuration
- formatter/lint configuration

Rules:

- Run checks directly affected by that configuration.
- Re-run CMake configure/build when build-system behavior can change.

### Environment variable changes

Examples:

- `.env`
- `.env.example`
- env loader code

Rules:

- Keep `.env.example` synchronized with code expectations.
- Use placeholder values only.
- Never commit real secrets.
- Run only verification relevant to env/config loading paths.

### Application code changes

For code modifications:

- format changed files when tooling exists
- build affected targets
- run focused tests when behavior is covered by tests
- run smoke checks for plugin/device flows when touching audio, graph scheduling, or plugin lifecycle

If uncertain whether broader verification is needed, run the Debug build and relevant smoke checks.

### Large or cross-cutting changes

Run full verification where available:

```bash
cmake --build build --config Debug --target VSTHostApp
ctest --test-dir build --output-on-failure
```

Also run relevant manual smoke checks.

## Smoke test expectation (device/plugin handling)

1. Launch the host.
2. Select an ASIO or WASAPI device.
3. Scan plug-ins and instantiate at least one VST3 effect.
4. Confirm audio passes and UI remains responsive.

## Verification reporting

In the final report, explain:

- which verification commands were run
- why they were required or skipped

If verification could not be run, explain why.

---

# Git & Documentation

- Use short imperative commit messages (<=72 chars), e.g., `Implement graph connection inspector`.
- Prefer Conventional Commits where practical (`feat:`, `fix:`, `refactor:`, `docs:`).
- Keep each commit to a single logical change.
- Group related changes together; avoid mixing refactors with behavioral changes unless necessary.

Pull requests should include:

1. summary of changes and rationale
2. testing commands with results
3. linked issues or TODO references where applicable
4. screenshots/GIFs for UI-impacting updates
5. risk callouts when touching device handling or plugin loading

Update `README.md` or relevant docs whenever external workflow or module boundaries shift.

---

# Security & Configuration

- Keep secrets in `.env` only (API tokens, credentials, DSNs, keys).
- Never commit real secrets.
- Do not log raw credentials or full connection strings.

## Environment variable sync rules

`.env.example` is the source-of-truth template for environment variables.

If code changes variable names, defaults, required flags, provider-specific variables, or meanings, update `.env.example` in the same change.

If code references a variable that does not appear in `.env.example`, the task is incomplete.
