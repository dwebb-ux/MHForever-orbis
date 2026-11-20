# MHForever Orbis

> **PlayStation 4-focused fork of MHForever** — evolving a PSP-centric runtime into a platform-agnostic architecture capable of supporting native PlayStation 4 homebrew targets.

> [!IMPORTANT]
> **Work in Progress:** No playable PS4 build currently exists. Development is actively progressing through runtime extraction, service ownership consolidation, memory tracking, and platform isolation required for future PlayStation 4 support.

---

## Project Status

| Area | Status | Notes |
| :--- | :---: | :--- |
| **Milestone 1: Foundation** | ✅ Complete | Runtime interfaces, allocators, manifest tooling & architecture docs |
| **Milestone 2: Runtime Extraction** | 🚧 In Progress | Dependency audit, service ownership, memory tracking, adapter migration, and platform isolation underway |
| **Milestone 3: PS4 Bring-Up** | 📋 Planned | OpenOrbis toolchain integration & backend initialization |
| **PS4 Renderer / Audio / Net** | ❌ Not Implemented | Future backend work |
| **FPKG / EBOOT / PARAM.SFO** | ❌ Not Implemented | Packaging & deployment pipeline pending |
| **Suspend / Resume** | ❌ Not Implemented | Future lifecycle work |
| **Hardware Validation** | ❌ Not Started | Pending PS4 bring-up |

### What Is Currently in the Repository

#### Runtime Foundation
- Platform-independent runtime interfaces
- `RuntimeContext`
- `ServiceRegistry`
- `RuntimeBootstrap`
- Concrete-only service registration
- Required-service validation through `Require<T>()`

#### Memory Infrastructure
- Arena allocator
- Linear frame allocator
- Memory tracking system (`MemoryTracker`)
- Memory budgets & categories
- High-water usage tracking & allocation reporting

#### Runtime Services
- `IRenderer`
- `IAudioSystem`
- `IInputSystem`
- `IFileSystem`
- `INetworkSystem`
- `ILogger`

#### Platform Adapters
- **PSP:** `PspFileSystem`, `PspNetworkSystem`, `PspRenderer` *(partial)*, `PspInputSystem` *(partial)*, `PspAudioSystem` *(partial)*
- **PS4:** `Ps4FileSystem`, `Ps4Input`, `Ps4Logger`

#### Tooling & Build System
- **Tooling:** Deterministic asset manifest generation, SHA-256 asset validation, content boundary validation, and runtime architecture documentation
- **Build System:** CMake `3.28+`, C++20, runtime test targets, platform-specific build configuration, and SDK-gated PS4 build support

### Not Yet Implemented

- Functional PS4 renderer, audio backend, or networking backend
- FPKG, EBOOT, and `PARAM.SFO` generation
- Suspend / resume lifecycle handling
- Real PS4 hardware validation

---

## Goals & Development Philosophy

The core principle is simple: **port the runtime, not the game.** Gameplay systems should remain platform-agnostic so new backends can be added without modifying game logic, prioritizing **correct architecture**, **platform isolation**, **runtime portability**, and **long-term maintainability**.

- **Native PlayStation 4 Homebrew Support:** Execute recompiled *Monster Hunter* content cleanly on PS4 homebrew environments once the runtime is decoupled.
- **Runtime Portability & Shared Architecture:** Keep a single runtime core supporting **PSP**, **Windows**, **Linux**, and **PlayStation 4** side by side.
- **Preservation:** Support long-term preservation of PSP-era *Monster Hunter* titles via static recompilation and modern runtime infrastructure.
- **Desktop Continuity:** Preserve compatibility across existing desktop targets while expanding console support.

---

## Why PlayStation 4 Is a Practical Target

- **Static Recompilation:** PSPRecomp statically translates PSP executables into native C/C++ ahead of time—avoiding dynamic recompilers, runtime code generation, executable memory permissions, and JIT restrictions.
- **Existing Dependency Availability:** Core dependencies such as **SDL** and **FFmpeg** are already available through **PacBrew OpenOrbis Portlibs**, reducing custom port work.
- **Host-Based Asset Processing:** ISO validation, asset extraction, runtime data generation, and manifest creation stay on desktop hosts, leaving the PS4 runtime to consume prepared data with lower memory overhead, faster startup, and simpler runtime logic.

---

## Architecture

```text
  Game Logic
       │
       ▼
  Runtime Layer
       │
       ▼
  Platform Interfaces
       │
       ▼
  Platform Backends
  ├── PSP
  ├── Windows
  ├── Linux
  └── PS4
```

### Repository Structure

```text
├── cmake/
├── docs/
│   └── architecture/
├── platforms/
│   ├── psp/
│   └── ps4/
├── runtime/
│   ├── core/
│   ├── interfaces/
│   ├── memory/
│   └── tests/
├── tests/
└── tools/
```

Platform-specific implementations live inside `platforms/`; runtime systems in `runtime/` remain platform-independent. Key architecture docs in `docs/architecture/` cover **Engine Principles**, **System Ownership**, **Threading Model**, **Memory Budget**, **PS4 Runtime Design**, **FPKG Build Pipeline**, and **Implementation Status**.

---

## Runtime Extraction Progress

The repository is actively migrating away from direct PSP/SDL coupling toward a runtime-owned service architecture:

| Current Architecture | Target Architecture |
| :--- | :--- |
| `Games` ──▶ `PSP Runtime` ──▶ `SDL / Platform Systems` | `Games` ──▶ `Runtime Interfaces` ──▶ `RuntimeContext` ──▶ `Platform Adapters` ──▶ `Platform Implementations` |

The runtime now owns service registration and lookup through `RuntimeContext`, `ServiceRegistry`, and `RuntimeBootstrap`. Future platform implementations are expected to register only concrete services and never placeholders.

---

## Runtime Service Ownership

The project enforces a strict **concrete-service registration model** so platforms expose only the functionality they actually implement while remaining compatible with the long-term architecture.

| Service | Purpose |
| :--- | :--- |
| `ILogger` | Diagnostics and logging |
| `IFileSystem` | File and asset access |
| `IInputSystem` | User input abstraction |
| `IAudioSystem` | Audio output abstraction |
| `INetworkSystem` | Network communication abstraction |
| `IRenderer` | Rendering abstraction |

**Registration & Access Rules:**
- No placeholder services or null service registration—unsupported services remain absent.
- Required services are accessed through `Require<T>()`.
- Optional services are accessed through `Get<T>()`.

---

## Build & Quickstart

### Requirements

- **CMake** `3.28+`
- **C++20 Compiler:** Clang or GCC
- **Python 3**
- **Desktop Dependencies:** SDL3 and FFmpeg

> [!NOTE]
> The PS4 target currently exists only as a gated platform boundary and infrastructure layer—it is **not** yet a playable PS4 port.

### Configure & Build

```bash
git clone [https://github.com/](https://github.com/)<your-username>/MHForever-Orbis.git
cd MHForever-Orbis

# Configure with Clang and Ninja
cmake -B build \
  -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build build -j
```

If `Ninja` is unavailable, omit `-G Ninja` to use your system's default CMake generator:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

---

## Asset Builder

Generates deterministic asset manifests for user-supplied game files using SHA-256 hashing, deterministic ordering, stable JSON output, and validation checks while separating repository code from user content.

```bash
python3 tools/mhf_asset_builder.py \
  /path/to/assets \
  --manifest manifest.json
```

The asset builder intentionally rejects known executable or packaged content formats when used incorrectly.

> [!WARNING]
> This repository does not distribute copyrighted game content.

---

## Milestone Progress

### ✅ Milestone 1 — Foundation *(Completed)*
Architecture documentation, system ownership/threading/memory models, PS4 runtime planning, runtime interfaces/context/service registry, arena & frame allocators, memory statistics, asset manifest tooling, SDK-gated PS4 platform boundary, and PSP adapter framework.

### 🚧 Milestone 2 — Runtime Extraction *(In Progress)*

**Objective:** Transform MHForever from a PSP-centric application into a platform-neutral runtime architecture.

- **Completed:**
  - ✅ Repository-wide dependency audit & runtime/platform dependency mapping
  - ✅ `RuntimeContext` ownership model & `ServiceRegistry` hardening
  - ✅ Concrete-only service registration & required-service enforcement (`Require<T>()`)
  - ✅ `MemoryTracker` & memory budgeting
  - ✅ SDL ownership audit, filesystem abstraction audit, networking architecture audit & PSP adapter inventory
- **In Progress:**
  - 🚧 PSP runtime bootstrap
  - 🚧 Frontend & host migration to `RuntimeContext`
  - 🚧 Adapter ownership consolidation
- **Planned:**
  - ⬜ Filesystem service adoption & networking transport/protocol separation
  - ⬜ SDL extraction from shared ownership areas
  - ⬜ PSP runtime service migration & platform ownership enforcement
- **Exit Criteria:** `RuntimeContext` owns service access; PSP adapters provide runtime services; game code depends solely on runtime interfaces; runtime contains no platform implementation details; SDL ownership is isolated to platform implementations.

### 📋 Milestone 3 — PS4 Bring-Up *(Planned)*
OpenOrbis integration, service initialization, platform bootstrap, filesystem/logging/input backends, and runtime lifecycle handling. Future graphics work will evaluate compatibility across the existing OpenGL renderer, OpenOrbis graphics pathways, and Piglet (OpenGL ES).

---

## Technologies

| Category | Technologies |
| :--- | :--- |
| **Runtime & Build** | C++20, CMake 3.28+, Clang, GCC |
| **Desktop & Media** | SDL3, FFmpeg, OpenGL *(existing renderer)* |
| **Recompilation** | PSPRecomp |
| **PS4 Target** | OpenOrbis, PacBrew OpenOrbis Portlibs, PlayStation 4 Homebrew |

---

## Credits

This project builds on the original **MHForever** codebase as well as the broader PSP recompilation and PlayStation 4 homebrew communities.

### Recompilation & Desktop Ecosystem

| Project / Library | Role | Link |
| :--- | :--- | :--- |
| **MHForever** *(Mango77x)* | Original PSP game recompilation project and workflow reference | [GitHub](https://github.com/Mango77x/MHForever) |
| **PSPRecomp** | Static recompilation framework enabling native execution | [GitHub](https://github.com/PSP-Archive/PSPRecomp) |
| **PPSSPP** | Technical reference for PSP behavior validation, emulation research, and compatibility | [GitHub](https://github.com/hrydgard/ppsspp) |
| **SDL** | Desktop runtime support for input, windowing, and audio | [GitHub](https://github.com/libsdl-org/SDL) |
| **FFmpeg** | Media decoding support used in existing infrastructure | [ffmpeg.org](https://ffmpeg.org/) |

### PlayStation 4 Homebrew Ecosystem

Special thanks to the maintainers and contributors building the modern PS4 homebrew SDK, package repositories, and port libraries:

| Project / Organization | Role | Link |
| :--- | :--- | :--- |
| **OpenOrbis** | Open-source PlayStation 4 development toolchain and SDK | [GitHub](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) |
| **PacBrew** | Maintainers of OpenOrbis package infrastructure and build ecosystem | [GitHub](https://github.com/PacBrew) |
| **ps4-openorbis-portlibs** | Prebuilt libraries including SDL, FFmpeg, and PS4-compatible dependencies | [GitHub](https://github.com/PacBrew/ps4-openorbis-portlibs) |

---

## Current Development Roadmap

| Phase | Focus Areas |
| :--- | :--- |
| **Milestone 2 — Runtime Extraction** | Runtime ownership, adapter migration, SDL isolation, filesystem abstraction, networking separation, platform bootstrapping |
| **Milestone 3 — PS4 Bring-Up** | OpenOrbis integration, runtime initialization, filesystem/logging/input backend completion, platform lifecycle |
| **Future Milestones** | PS4 renderer/audio/networking implementation, asset deployment workflow, FPKG generation, performance validation, hardware testing |

---

## Legal

> [!WARNING]
> **No Copyrighted Assets Included:** This repository contains source code, tooling, and documentation only. Users must provide their own legally owned game data.
>
> *Monster Hunter* and related intellectual property belong to **Capcom Co., Ltd.** This project is not affiliated with, endorsed by, sponsored by, or approved by Capcom.

---

## Current State Summary

| Component / Stage | State |
| :--- | :--- |
| Architecture Foundation | ✅ Complete |
| Runtime Ownership | ✅ Established |
| Memory Infrastructure | ✅ Established |
| Platform Boundaries | ✅ Established |
| Runtime Extraction | 🚧 In Progress |
| PSP Adapter Migration | 🚧 In Progress |
| PS4 Bring-Up | 📋 Planned |
| PS4 Renderer / Audio / Networking | ❌ Not Implemented |

### PS4 Runtime Direction

The project is evaluating PS4-Kit as the primary PlayStation 4 platform backend. 
https://github.com/iHaiDeeZ/ps4-kit

PS4-Kit provides:

- OpenOrbis integration
- Runtime facilities
- File layer
- Diagnostics
- Packaging scripts
- Vulkan rendering support

Future PS4 work may build on these components
rather than implementing equivalent systems
from scratch.
