# MHForever Orbis

> **PlayStation 4-focused fork of MHForever** — evolving a PSP-centric runtime into a platform-agnostic architecture capable of supporting native PlayStation 4 homebrew targets.

> [!IMPORTANT]
> **Work in Progress:** No playable PS4 build currently exists. Development currently focuses on runtime extraction, platform abstraction, build modernization, tooling, and infrastructure required for future PS4 support.

---

## Project Status

| Area | Status | Notes |
| :--- | :---: | :--- |
| **Milestone 1: Foundation** | ✅ Complete | Core abstractions, allocators, manifest tooling & boundaries |
| **Milestone 2: Runtime Extraction** | 🚧 In Progress | Extracting adapters & isolating platform/SDL dependencies |
| **Milestone 3: PS4 Bring-Up** | 📋 Planned | Orbis toolchain, PS4 backends & startup lifecycle |
| **PS4 Renderer / Audio / Net** | ❌ Not Yet Implemented | Future backend work |
| **FPKG / EBOOT / PARAM.SFO** | ❌ Not Yet Implemented | Packaging & deployment pipeline pending |
| **Suspend / Resume & Hardware** | ❌ Not Yet Implemented | Pending PS4 bring-up and hardware validation |

### What Is Currently in the Repository

- **Runtime & Core Systems:** Runtime abstraction layer, core service registration framework, and platform-independent interfaces
- **Memory Management:** Memory allocator framework (arena allocator, frame allocator, memory statistics)
- **Tooling & Boundaries:** Asset validation/manifest tooling, SDK-gated PS4 platform boundary, and PSP adapter migration infrastructure
- **Build System:** Modernized CMake configuration

### What Is Not Yet Included

- Functional PS4 renderer, audio backend, or networking backend
- FPKG, EBOOT, or `PARAM.SFO` generation
- Suspend/resume implementation
- Real PS4 hardware validation

---

## Goals

- **Native PlayStation 4 Homebrew Support:** Target PS4 homebrew environments cleanly once the runtime layer is decoupled.
- **Runtime Portability:** Keep the core runtime platform-agnostic across consoles and desktop targets.
- **Preservation:** Support preservation of PSP-era *Monster Hunter* titles through clean recompilation infrastructure.
- **Shared Runtime Architecture:** Maintain a unified runtime core across backends.
- **Desktop Continuity:** Preserve Windows and Linux support alongside future console backends.

---

## Architecture

Gameplay systems should remain decoupled from platform specifics so new backends can be added without modifying game logic.

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

---

## Build & Quickstart

### Prerequisites

- **CMake** `3.28+` (Ninja, Unix Makefiles, or Visual Studio generator)
- **Clang** toolchain (`clang` / `clang++`) with C++20 support
- **SDL3** and host runtime dependencies (for desktop/host build targets)
- **Python 3** (if running or scripting Asset Builder manifest workflows)

> [!NOTE]
> The PS4/Orbis target boundary is currently SDK-gated and not yet a playable PS4 build. Start with a standard host build to verify runtime extraction, tests, and tooling.

### Configure & Build (Clang)

```bash
git clone [https://github.com/](https://github.com/)<your-username>/MHForever-Orbis.git
cd MHForever-Orbis

cmake -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build -j
```

If you do not use `Ninja`, omit `-G Ninja` to use your system's default CMake generator.

### Running Asset Builder (Manifest Generation)

Point the Asset Builder at a directory containing your own legally obtained game files to generate a deterministic manifest with SHA-256 checksums:

```bash
./build/asset_builder /path/to/your/game/assets --output build/manifest.json
```

*(Adjust the binary name and CLI flags to match your exact build target output.)*

---

## Milestone Progress

### ✅ Milestone 1 — Foundation *(Complete)*

- **Architecture & Models:** Architecture documentation, ownership model, threading model, memory model, and PS4 runtime planning
- **Runtime & Services:** Runtime interfaces, runtime context, and service registry
- **Allocators & Diagnostics:** Arena allocator, frame allocator, and memory statistics
- **Tooling & Adapters:** Asset manifest tool, SDK-gated PS4 platform boundary, and PSP adapter framework

### 🚧 Milestone 2 — Runtime Extraction *(In Progress)*

- **PSP Adapters:** Renderer, audio, input, filesystem, and networking adapters
- **Decoupling & Cleanup:** Runtime adoption across existing code, SDL extraction from shared ownership areas, and dependency isolation

### 📋 Milestone 3 — PS4 Bring-Up *(Planned)*

- **Toolchain & Lifecycle:** Orbis toolchain integration, startup lifecycle, and service initialization
- **PS4 Backends:** Filesystem, logging, and input backends

---

## Tooling

### Asset Builder

Generates deterministic manifests for user-supplied game assets.

- **SHA-256 Generation:** Cryptographic hashes for asset integrity
- **Deterministic Output:** Stable manifests with deterministic ordering
- **Validation & Separation:** Manifest validation with clear separation between repository code and user content

> [!NOTE]
> This repository does not distribute game assets. Users must supply their own legally owned files.

---

## Tech Stack

| Category | Technologies |
| :--- | :--- |
| **Core & Desktop** | C++20, CMake 3.28+, Clang, SDL3, OpenGL *(existing implementation)*, PSPRecomp ecosystem |
| **Target Platform (PS4)** | OpenOrbis Toolchain, PacBrew Portlibs, PlayStation 4 Homebrew, FPKG Deployment Pipeline |

---

## Credits

This fork builds on the foundation laid by the original **MHForever** project and the broader PSP recompilation community.

### Core Recompilation & Desktop Libraries

| Project / Library | Role & Contribution | Link |
| :--- | :--- | :--- |
| **MHForever** *(Mango77x)* | Original project demonstrating practical PSP game recompilation workflows | [GitHub](https://github.com/Mango77x/MHForever) |
| **PSPRecomp** | Recompilation framework enabling PSP game recompilation workflows | [GitHub](https://github.com/PSP-Archive/PSPRecomp) |
| **PPSSPP** | Technical reference for PSP behavior validation, system research, and compatibility testing | [GitHub](https://github.com/hrydgard/ppsspp) |
| **SDL** | Existing desktop runtime and host system support | [GitHub](https://github.com/libsdl-org/SDL) |
| **FFmpeg** | Media processing components used in existing MHForever infrastructure | [ffmpeg.org](https://ffmpeg.org/) |

### PS4 Development Ecosystem

Special thanks to the teams and maintainers building the open-source PS4 homebrew toolchain and port library ecosystem:

| Project / Organization | Role & Contribution | Link |
| :--- | :--- | :--- |
| **OpenOrbis** | Open-source PlayStation 4 homebrew SDK and toolchain | [GitHub](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) |
| **PacBrew** | Maintainers of PS4 OpenOrbis package repositories, toolchains, and precompiled port libraries | [GitHub](https://github.com/PacBrew) |
| **ps4-openorbis-portlibs** | Provides SDL, FFmpeg, and additional libraries required for PlayStation 4 homebrew development | [GitHub](https://github.com/PacBrew/ps4-openorbis-portlibs) |

---

## Legal

> [!WARNING]
> **No Copyrighted Assets Included:** This repository contains only source code, tooling, and documentation. Users must provide their own legally owned game assets.
>
> *Monster Hunter* and related intellectual property belong to **Capcom Co., Ltd.** This project is not affiliated with, endorsed by, or sponsored by Capcom.
