# PS4 Remediation Implementation Status

Version: 1.0

Status: Transitional

## Purpose

Record the implementation state of the PS4 architecture migration.

This document distinguishes verified source and build behavior from
capabilities that require an authorized PlayStation 4 SDK and hardware.

## Implemented

### Runtime Interfaces

The following platform-independent contracts are implemented under
`runtime/interfaces/`:

- `IRenderer`
- `IAudioSystem`
- `IInputSystem`
- `IFileSystem`
- `INetworkSystem`
- `ILogger`

These interfaces contain no PS4 SDK symbols and may be implemented by
Windows, Linux, or PS4 backends.

### Bootstrap Policy

`RuntimeContext` contains only concrete service implementations.

Services are registered only when a platform provides a working
implementation. Unsupported services are absent rather than represented by
placeholders.

Consumers should use `Get<T>()` for optional services and `Require<T>()` for
mandatory services.

### Memory Infrastructure

`runtime/memory/` provides:

- alignment-aware linear frame allocation
- alignment-aware arena allocation
- allocation counts
- high-water marks
- capacity and used-byte statistics
- largest remaining free-block reporting

The allocator test is registered as `runtime_memory_allocator`.

### PS4 Platform Boundary

`platforms/ps4/` contains the PS4-only boundary and is excluded unless
`MHF_BUILD_PS4=ON` is combined with the Orbis toolchain.

The PS4 toolchain file validates `SCE_ORBIS_SDK_DIR`.

Implemented PS4 adapters:

- `Ps4Logger`
- `Ps4FileSystem`
- `Ps4Input`

No desktop build substitutes for these SDK adapters.

### Asset and Package Tooling

`tools/mhf_asset_builder.py`:

- validates caller-owned package roots
- refuses executable and game-data suffixes
- refuses build and generated repository paths
- creates deterministic JSON hash manifests
- invokes an external PS4 SDK package tool only with explicit inputs

## SDK-Gated and Not Yet Verified

The following remain unverified until a valid Orbis SDK and PS4 SDK toolchain
are available:

- PS4 render backend and video output
- PS4 audio backend
- PS4 network transport
- PS4 EBOOT generation
- PS4 PARAM.SFO generation
- FPKG signing and package creation
- PS4 suspend/resume integration
- On-device memory confirmation
- 60 FPS performance targets

The repository does not contain placeholder PS4 implementations for these
services.  Missing SDK behavior must not be represented as verified.

## Remaining Migration

The existing PSP implementation remains in `platforms/psp/` during the
incremental migration.

The following PSP and desktop dependencies must remain outside gameplay:

- SDL implementation
- Windows/POSIX socket implementation
- PSP HLE and frontend implementation
- generated game host bindings

Before PS4 runtime acceptance, a platform-neutral runtime target must own:

- execution scheduling
- save format and migration
- asset streaming
- protocol structures
- memory category services
- diagnostics

Before FPKG acceptance, the external SDK pipeline must be validated on actual
PlayStation 4 hardware or an SDK-supported development environment.
