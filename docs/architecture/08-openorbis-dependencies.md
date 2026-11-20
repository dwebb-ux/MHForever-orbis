# OpenOrbis Dependencies

Version: 1.0

Status: Foundational

## Purpose

Define the build, runtime, and tooling dependencies for the MHForever PS4
target. This document separates dependencies required for a supported build
from optional integrations and future capabilities.

## Required

### OpenOrbis

OpenOrbis provides the PS4 homebrew toolchain and SDK-compatible build
environment used by Orbis targets.

- Used by: `platforms/ps4/`
- Build role: compilation, linking, and PS4 executable generation
- Integration rule: SDK-specific symbols and headers remain inside the PS4
  platform boundary

### PacBrew

PacBrew provides PS4 development tools and package-related utilities.

- Used by: PS4 build and package tooling
- Build role: toolchain support and deployment utilities
- Integration rule: package operations must run through repository tooling
  and must not be invoked directly by gameplay code

### SDL

SDL is the desktop host dependency.

- Used by: desktop window, input, and audio hosts
- CMake option: `MHF_WITH_SDL`
- Default: enabled
- Integration rule: SDL types and APIs remain in platform or host code

### FFmpeg

FFmpeg provides media decoding.

- Used by: video and music decoding
- Linked components: the decoders and utilities required by the runtime
- Build role: project-built or system-provided according to the platform
  build configuration
- Integration rule: media decoding remains outside gameplay state and uses
  runtime-owned interfaces

## Optional

### Piglet

Piglet is an optional PS4 graphics or graphics-library integration.

- Status: not a current build requirement
- Enablement: only when a supported implementation is selected
- Integration rule: graphics APIs must remain inside `platforms/ps4/` or a
  replaceable renderer implementation

## Future

### Audio

Future work includes expanding the PS4 audio backend beyond its initial
platform implementation.

- `IAudioSystem` remains the runtime abstraction
- SDK and hardware-specific behavior remains platform-owned
- suspend/resume behavior must be tested on supported hardware

### Networking

Future work includes PS4 transport and protocol integration.

- `INetworkSystem` remains the runtime abstraction
- platform-neutral protocol structures remain outside the PS4 backend
- networking behavior must not depend on desktop-only socket
  implementations

### Package Toolchain

Future work includes a complete reproducible FPKG pipeline.

- deterministic inputs and asset verification
- EBOOT and PARAM.SFO generation
- package creation and signing
- validation and release artifact reporting

The package toolchain must use explicit inputs and an authorized SDK or
development environment. Repository source must not contain credentials,
signing secrets, or generated package artifacts.
