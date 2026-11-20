# MHForever Orbis

> **PlayStation 4-focused fork of MHForever** — evolving a PSP-centric runtime into a platform-agnostic architecture capable of supporting native PlayStation 4 homebrew targets.

> [!IMPORTANT]
> **Work in Progress:** No playable PS4 build currently exists. Development focuses on runtime extraction, platform abstraction, build-system modernization, tooling, and infrastructure required for future PlayStation 4 support.

---

## Project Status

| Area | Status | Notes |
| :--- | :---: | :--- |
| **Milestone 1: Foundation** | ✅ Complete | Runtime interfaces, allocators, manifest tooling & architecture docs |
| **Milestone 2: Runtime Extraction** | 🚧 In Progress | PSP adapter migration, SDL isolation & platform boundary work |
| **PSP GE Graphics Backends** | 🚧 In Progress | OpenGL 3.3 backend active (default); Vulkan backend through Phase 2C complete |
| **Milestone 3: PS4 Bring-Up** | 📋 Planned | OpenOrbis toolchain integration & backend initialization |
| **PS4 Renderer / Audio / Net** | ❌ Not Implemented | Future backend work |
| **FPKG / EBOOT / PARAM.SFO** | ❌ Not Implemented | Packaging & deployment pipeline pending |
| **Suspend / Resume** | ❌ Not Implemented | Future lifecycle work |
| **Hardware Validation** | ❌ Not Started | Pending PS4 bring-up |

### Current Repository State

**Implemented:**
- **Runtime & Architecture:** Platform-independent interfaces, runtime context, service registry, memory allocators (arena, frame, stats), asset validation tooling, and deterministic manifest generation.
- **Backend Selection & Factory:** Unified `gpu::RenderConfig` and factory (`make_backend`) supporting selectable OpenGL (`BackendKind::Gl`) and Vulkan (`BackendKind::Vulkan`) backends.
- **SDL Surface Handoff:** Window creation decoupled in `SdlHost` to support `SDL_WINDOW_VULKAN` without forcing OpenGL context creation when Vulkan is selected.
- **CPU Texture Cache Decoupling:** Shared, backend-agnostic CPU texture decoding, CLUT lookup, cache-key hashing, and HD text page reconstruction (`TextPageCache`) independent of GPU upload paths.
- **Vulkan GE Backend (Phases 0 – 2C Complete):**
  - **Phase 1 (POC):** Device/instance setup, Vulkan SDL surface creation, swapchain management, frame synchronization (`kMaxFramesInFlight = 2`), and clear-and-present loop.
  - **Phase 2A & 2B (Core Geometry & Pipelines):** Build-time GLSL -> SPIR-V compilation (`glslc`), persistently mapped per-frame vertex/sprite ring buffers, push constants, dynamic viewport/scissor, GE register snapshotting, and `VkPipeline` state caching.
  - **Phase 2C (Render Targets & Readbacks):** Offscreen `RenderTarget` pool with `render_scale` support, overlap-safe framebuffer transfers/blits (`block_transfer`), and direct staging readbacks (`copy_to_ram`) back to guest VRAM.
- **Shader & Test Infrastructure:** Automated SPIR-V compilation target (`mhf_psp_vulkan_shaders`), validation (`spirv-val`), and unit test suite (`test_psp_vulkan_backend`, `test_texture_cache`) verified under headless Xvfb/lavapipe execution.

**In Progress / Pending (Phase 3):**
- **Phase 3 Vulkan Features:** Full GPU texture-pack and HD-text upload integration, frame interpolation pass, and projected shadow rendering pipeline.

**Not Yet Implemented:**
- Functional native PS4 renderer, audio backend, or networking backend
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
  ├── PSP (GL 3.3 / Vulkan)
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
│   │   ├── frontend/
│   │   │   ├── shaders/        # GLSL sources for Vulkan SPIR-V target
│   │   │   ├── gl_backend.cpp  # OpenGL 3.3 Core GE backend
│   │   │   ├── vulkan_backend.cpp # Vulkan GE backend (Phases 0-2C)
│   │   │   └── texture_cache.cpp  # Backend-agnostic CPU texture/HD-text cache
│   │   └── gpu/
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

## Build & Quickstart

### Requirements

- **CMake** `3.28+`
- **C++20 Compiler:** Clang or GCC
- **Python 3**
- **Desktop Dependencies:** SDL3 and FFmpeg (`libavcodec`, `libavutil`)
- **Vulkan Toolchain (Optional for Vulkan Backend):** `libvulkan-dev`, `glslc` (from `vulkan-tools` / `glslang-tools`), and `spirv-val`

> [!NOTE]
> The PS4 target currently exists as a gated platform boundary and infrastructure layer—it is **not** yet a playable PS4 port.

### Configure & Build

```bash
git clone https://github.com/<your-username>/MHForever-Orbis.git
cd MHForever-Orbis

# Configure with Clang, Ninja, and Vulkan/SDL/FFmpeg flags
cmake -B build \
  -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMHF_WITH_SDL=ON \
  -DMHF_FFMPEG=system \
  -DMHF_BUILD_TESTS=ON

# Build all targets including Vulkan SPIR-V shaders and test targets
cmake --build build -j
```

If `Ninja` is unavailable, omit `-G Ninja` to use your system's default CMake generator:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

### Running Tests

Run the PSP GPU backend and texture cache tests (including headless Vulkan validation):

```bash
# Run tests directly or via CTest
xvfb-run -a ctest --test-dir build -R 'psp_vulkan_backend|psp_texture_cache' --output-on-failure
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

### 🚧 Milestone 2 — Runtime Extraction *(Current)*
Migrating PSP renderer, audio, input, filesystem, and networking adapters behind runtime interfaces while isolating SDL dependencies, cleaning up dependencies, and adopting the runtime across existing systems:

```text
Existing PSP Implementation ──▶ Runtime Interfaces ──▶ Platform Adapters
                                                       ├── OpenGL 3.3 Backend (Default)
                                                       └── Vulkan Backend (Phases 0–2C)
```

### 📋 Milestone 3 — PS4 Bring-Up *(Planned)*
OpenOrbis integration, service initialization, platform bootstrap, filesystem/logging/input backends, and runtime lifecycle handling. Future graphics work will evaluate compatibility across the existing OpenGL renderer, Vulkan backend, OpenOrbis graphics pathways, and Piglet (OpenGL ES).

---

## Technologies

| Category | Technologies |
| :--- | :--- |
| **Runtime & Build** | C++20, CMake 3.28+, Clang, GCC |
| **Desktop & Media** | SDL3, FFmpeg, OpenGL 3.3 Core, Vulkan 1.0+, GLSL / SPIR-V (`glslc`) |
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

## Legal

> [!WARNING]
> **No Copyrighted Assets Included:** This repository contains source code, tooling, and documentation only. Users must provide their own legally owned game data.
>
> *Monster Hunter* and related intellectual property belong to **Capcom Co., Ltd.** This project is not affiliated with, endorsed by, sponsored by, or approved by Capcom.
