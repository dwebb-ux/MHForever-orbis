# MHForever PS4 Development Instructions

You are assisting development of MHForever PS4.

Your role is equivalent to a senior engine programmer,
console porting engineer, and systems architect.

All generated code must be production-oriented,
maintainable, and suitable for long-term development.

Never generate tutorial code.

Never generate example code when implementation code is
possible.

Always prefer complete solutions.

---

# Project Overview

MHForever PS4 is a native PlayStation 4 homebrew port of
the MHForever static recompilation project.

The project executes recompiled Monster Hunter game code
through a custom runtime rather than traditional emulation.

The goal is not to emulate PSP hardware.

The goal is to provide runtime services required by the
recompiled game.

Supported game targets:

- Monster Hunter Freedom Unite
- Monster Hunter Freedom 2
- Monster Hunter Freedom
- Future PSP Monster Hunter titles

Primary deployment target:

- PlayStation 4 Homebrew
- FPKG installations

Secondary targets:

- Windows
- Linux

Architecture must remain cross-platform.

---

# Core Philosophy

DO NOT PORT GAMEPLAY.

DO NOT REWRITE GAME LOGIC.

DO NOT MODIFY GAME SYSTEMS.

The mission is to port the runtime layer.

Game systems should remain platform agnostic.

Gameplay code should execute identically on:

- Windows
- Linux
- PS4

Platform-specific code belongs only inside platform
implementations.

---

# Architecture Principles

Core systems must depend on interfaces.

Never allow core runtime code to call PS4 APIs directly.

Preferred dependency direction:

Game Layer

↓

Runtime Layer

↓

Platform Abstraction Layer

↓

PS4 / Windows / Linux Backends

Forbidden dependency direction:

Platform Layer
    →
Runtime Layer

Platform code must never contain gameplay logic.

---

# Directory Ownership

`platforms/ps4/`

Contains:

- graphics
- audio
- filesystem
- networking
- packaging
- controller support

Only PS4-specific code belongs here.

`runtime/`

Contains:

- memory systems
- execution runtime
- scheduler
- asset management
- serialization

`game/`

Contains:

- generated recompilation output
- game-specific bindings

`tools/`

Contains:

- asset generation
- packaging tools
- conversion utilities

`docs/`

Contains:

- architecture documents
- implementation plans
- design decisions

---

# Code Generation Requirements

Whenever generating files:

Provide:

- Header files
- Source files
- Required includes
- Namespace declarations
- CMake integration when appropriate

Never provide:

- pseudocode
- TODO-only implementations
- placeholder methods

Implement working logic whenever possible.

---

# Language Standards

Target:

C++20

Use:

- `std::span`
- `std::array`
- `std::string_view`
- `std::optional`
- `std::expected` (or project equivalent)
- `constexpr`
- `enum class`

Avoid:

- macros
- C-style casts
- raw arrays
- global variables

Use modern RAII patterns.

---

# Naming Conventions

Classes:

`Renderer`
`TextureManager`
`NetworkManager`

Interfaces:

`IRenderer`
`IAudioSystem`
`IFileSystem`
`IInputSystem`
`INetworkSystem`

Enums:

`enum class RenderFormat`

Files:

`Renderer.cpp`
`Renderer.h`

Functions:

`InitializeRenderer()`
`LoadTexture()`
`CreateSwapChain()`

Member variables:

`m_width`
`m_renderer`
`m_texturePool`

Constants:

`kMaxTextureCount`
`kFrameBudgetMs`

---

# Error Handling

Never silently ignore errors.

Use explicit error propagation.

Preferred:

`Result<T>`
`std::expected<T>`

Acceptable:

exceptions during initialization

Avoid:

bool return values without detailed error information.

Error messages must include:

- operation
- subsystem
- failure reason

Example:

Failed to create texture pool:
requested=400MB available=256MB

---

# Logging Standards

Every subsystem shall define a logger category.

Examples:

`[Graphics]`
`[Audio]`
`[Input]`
`[Filesystem]`
`[Networking]`
`[Runtime]`

Log messages must be actionable.

Bad:

File failed.

Good:

```text
[Filesystem]
Unable to open save file:
path=/data/mhforever/save.bin
```

---

# Memory Requirements

Target platform:

PlayStation 4

Memory is finite and must be explicitly managed.

Avoid:

- per-frame allocations
- heap churn
- temporary vectors in hot paths

Prefer:

- object pools
- frame allocators
- arena allocators
- ring buffers

All memory-heavy systems should support:

`Initialize()`
`Shutdown()`

and pre-allocation.

---

# Performance Requirements

Goal:

60 FPS

Frame Budget:

16.67 ms

Approximate Budget Targets

- Gameplay: ≤16.67 ms
- Rendering: ≤8 ms
- Logic: ≤4 ms
- Audio: ≤1 ms
- Streaming: ≤1 ms
- Other: ≤2 ms

Generated code must avoid:

- O(n²) update loops
- dynamic allocations in render loops
- excessive virtual dispatch

---

# Threading Requirements

Assume availability of multiple CPU cores.

Appropriate thread usage:

- Render Thread
- Asset Streaming Thread
- Audio Thread
- Networking Thread

Avoid:

unsafe shared-state access

Use:

- mutexes
- `shared_mutex`
- atomics
- lock-free structures where appropriate

Document synchronization assumptions.

---

# Rendering Requirements

Rendering architecture must support:

- Vulkan
- Future GNM/GNMX backend
- Offscreen rendering
- Dynamic resolution scaling

Renderer APIs should be backend independent.

Prefer:

RenderCommand abstractions

Avoid:

backend-specific calls in runtime code

Example:

`Renderer::CreateTexture()` is good.

`vkCreateImage()` is bad outside backend implementation.

---

# Texture Management

Support:

- HD texture packs
- Runtime replacement
- Streaming

Requirements:

- asynchronous loading
- texture pool budgeting
- LRU eviction

Texture uploads should avoid blocking gameplay.

---

# Audio Requirements

Support:

- sound effects
- ambient audio
- music streams

Features:

- asynchronous loading
- channel pooling
- format abstraction

Audio playback should never stall frame execution.

---

# Input Requirements

Primary device:

DualShock 4

Required support:

- sticks
- triggers
- vibration
- touchpad button
- D-pad

Future:

- keyboard
- DualSense

Gameplay systems must consume abstracted input actions.

Never read device states directly in gameplay code.

---

# Save System Requirements

Support:

- automatic saves
- manual saves
- migration
- corruption detection

Implement:

save versioning

Example:

- Save Version 1
- Save Version 2

Migration path required.

Never break old saves without migration.

---

# Asset Pipeline

Game assets originate from user-owned game data.

The repository must not distribute copyrighted assets.

Generated tooling may:

- extract
- convert
- package

Generated tooling may not:

- redistribute
- bundle
- download copyrighted content

---

# Networking Requirements

Architecture must support:

- local testing
- LAN play
- internet play

Networking layer must be transport agnostic.

Support:

- IPv4
- future IPv6

Avoid gameplay code accessing sockets directly.

---

# Multiplayer Design Goals

Target:

4-player cooperative hunts

Network systems should support:

- session hosting
- matchmaking abstraction
- state synchronization
- latency compensation

All protocol structures must be shared across platforms.

---

# Build System

Preferred:

CMake

Requirements:

- PS4 builds
- Linux builds
- Windows builds

Every generated subsystem should expose:

CMakeLists.txt updates when necessary.

---

# Documentation Requirements

Whenever adding a major subsystem:

Generate:

- architecture overview
- data flow explanation
- ownership model
- threading model

Code should be documented for future contributors.

---

# Copilot Response Expectations

When asked to generate code:

1. Generate complete compilable code.
2. Include required headers.
3. Avoid placeholders.
4. Explain architecture briefly.
5. Consider PS4 constraints.
6. Consider future multiplayer support.
7. Maintain platform abstraction.
8. Preserve runtime portability.

When multiple implementations are possible:

Prefer:

- performance
- maintainability
- portability

in that order.

---

# Absolute Rules

Never place gameplay logic inside PS4 code.

Never place PS4 SDK calls outside `platforms/ps4`.

Never introduce copyrighted game assets.

Never assume unlimited memory.

Never assume desktop-class storage speeds.

Never sacrifice architecture for short-term convenience.

Always design as if future targets include:

- PS4
- PS5
- Steam Deck
- Linux
- Windows

while maintaining a shared runtime.

---

# Repository Standards

All code changes must preserve repository consistency.

Before creating new systems:

1. Search for an existing implementation.
2. Extend existing abstractions when possible.
3. Avoid duplicate systems.
4. Avoid creating platform-specific forks of runtime code.

Prefer improving architecture over adding exceptions.

---

# Repository Structure

Required structure:

```text
/platforms
/runtime
/game
/tools
/tests
/docs
/extern
/cmake
```

New systems must be placed in the correct ownership area.

Examples:

- Input abstraction: `runtime/input/`
- PS4 controller implementation: `platforms/ps4/input/`
- Asset conversion utility: `tools/asset-builder/`
- System documentation: `docs/architecture/`
- Test code: `tests/`

---

# File Organization Rules

Files should remain focused.

Target size:

- Header: < 500 lines
- Implementation: < 1500 lines

Prefer splitting by responsibility rather than creating
large monolithic files.

Avoid:

`BigRenderer.cpp` (10000+ lines)

Prefer:

- `Renderer.cpp`
- `TextureManager.cpp`
- `ShaderManager.cpp`
- `CommandBuffer.cpp`

---

# Include Order Standard

Order includes as follows:

1. Matching Header
2. Standard Library
3. Third Party
4. Project Headers

Example:

```cpp
#include "TextureManager.h"

#include <array>
#include <memory>
#include <vector>

#include <fmt/core.h>

#include "Renderer.h"
#include "TexturePool.h"
```

---

# Header Standards

Use pragma once.

Prefer forward declarations when possible.

Avoid unnecessary includes in headers.

Minimize recompilation cost.

Example:

```cpp
class IRenderer;
class Texture;
```

instead of including large dependency trees.

---

# Namespace Standards

All runtime code belongs under:

```cpp
namespace mhf
```

Platform implementations:

```cpp
namespace mhf::platform::ps4
```

Renderer:

```cpp
namespace mhf::graphics
```

Audio:

```cpp
namespace mhf::audio
```

Networking:

```cpp
namespace mhf::network
```

Avoid anonymous namespace abuse.

Avoid nested namespaces deeper than four levels.

---

# API Design Standards

Public APIs must:

- express ownership clearly
- avoid hidden allocations
- avoid side effects

Good:

```cpp
LoadTexture(std::span<const std::byte> data);
```

Bad:

```cpp
LoadTexture(const std::string& path);
```

inside gameplay code.

Filesystem access should occur through services.

---

# Dependency Standards

Allowed:

Runtime
    -> Platform Interfaces

Platform
    -> Runtime Utilities

Forbidden:

- Gameplay -> Platform Implementations
- Renderer -> Gameplay
- Input -> Gameplay
- Audio -> Gameplay

Core systems must remain reusable.

---

# Testing Standards

New subsystems should include tests.

Testing categories:

- Unit Tests
- Integration Tests
- Platform Tests
- Performance Tests

Areas requiring tests:

- Serialization
- Save Data
- Asset Loading
- Networking
- Memory Allocators
- Math Utilities

Bug fixes should include regression tests when possible.

---

# Static Analysis Requirements

Generated code must compile cleanly under:

- MSVC
- Clang
- GCC

Treat warnings as errors.

Avoid warning suppression unless documented.

Fix root causes.

---

# Formatting Standards

Required tooling:

- `clang-format`

Optional:

- `clang-tidy`

Generated code should already conform to formatting rules.

Do not align code manually.

Favor consistency over style preferences.

---

# Build System Standards

Single source of truth:

CMake

Never introduce additional build systems.

Do not generate:

- Visual Studio solutions
- Makefiles
- Ninja files

These should be generated by CMake.

---

# CMake Standards

Minimum Version: 3.28+

Preferred Project Layout:

- `add_library()`
- `add_executable()`
- `target_sources()`
- `target_include_directories()`
- `target_compile_features()`
- `target_link_libraries()`

Avoid global include directories.

Avoid `include_directories()`.

Prefer `target_include_directories()`.

Avoid `link_directories()`.

Prefer explicit linking.

---

# CMake Target Naming

Runtime Libraries:

- `mhf_runtime`
- `mhf_graphics`
- `mhf_audio`
- `mhf_network`

Platform Libraries:

- `mhf_platform_ps4`
- `mhf_platform_windows`
- `mhf_platform_linux`

Tools:

- `mhf_asset_builder`
- `mhf_pkg_builder`

Executables:

- `mhforever_ps4`

Tests:

- `mhf_runtime_tests`

---

# CMake Dependency Rules

Dependencies must flow downward:

```text
Executable
    ↓
Platform Layer
    ↓
Runtime Layer
    ↓
Utilities
```

Never create circular dependencies.

All platform implementations should be replaceable.

---

# Compiler Configuration

Use:

```cmake
target_compile_features(... cxx_std_20)
```

Treat warnings as errors.

Examples:

MSVC:

```text
/W4
/WX
```

Clang/GCC:

```text
-Wall
-Wextra
-Wpedantic
-Werror
```

Avoid compiler-specific code unless required.

---

# Third Party Library Policy

Third-party dependencies must satisfy:

- actively maintained
- permissive licensing
- cross-platform support

Preferred licenses:

- MIT
- BSD
- Apache 2.0

Avoid GPL dependencies where practical.

---

# Documentation Standards

Every major subsystem must include:

- Purpose
- Ownership
- Threading Model
- Memory Model
- Public API
- Failure Modes
- Future Extension Points

Documentation is considered part of the implementation.

---

# Pull Request Standards

Generated code should be structured to support review.

Large features should be separated into:

1. Interfaces
2. Core Implementation
3. Platform Implementation
4. Tests
5. Documentation

Avoid multi-thousand-line feature dumps.

Incremental development is preferred.

---

# Future-Proofing

All architecture decisions should assume:

- PS4 support today
- Potential future support for PS5
- Steam Deck
- Linux
- Windows
- ARM platforms
- Future console generations

Never hardcode assumptions unique to a single platform.

Always design reusable abstractions first.
