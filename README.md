# MHForever PS4

Native PlayStation 4 homebrew port of MHForever.

MHForever PS4 extends the MHForever static recompilation
project with support for PlayStation 4 homebrew and FPKG
deployments.

Goals:

- Native execution on PS4
- 60 FPS gameplay
- DualShock 4 support
- HD rendering
- Save compatibility with PC builds
- Future multiplayer support

This project does not distribute copyrighted game data.

Users must provide their own legally obtained game assets.

## Supported Targets

- PS4 Fat
- PS4 Slim
- PS4 Pro

## Supported Firmware (planned)

- 9.00
- 11.00
- Future jailbreak-supported releases

## Current Status

- Platform design
- Runtime integration
- PSP graphics backend development (Vulkan through Phase 2C)

## PSP Graphics Backend

The PSP frontend currently has two GPU backends:

- OpenGL (`BackendKind::Gl`) remains the default and established rendering path.
- Vulkan (`BackendKind::Vulkan`) is in development. Phase 1 adds surface and
  swapchain initialization with clear-and-present. Phases 2A and 2B add
  build-time SPIR-V shaders, pipeline creation/caching, and basic batched
  triangle/sprite drawing.
  Phase 2C adds PSP-addressed offscreen render targets, blits, block transfers,
  framebuffer readbacks, and overlay compositing.

The Vulkan backend is not yet a full replacement for OpenGL. Phase 3 remains:
texture-pack and HD text uploads, frame interpolation, and projected shadows.
CPU-side guest texture decoding and HD text-page reconstruction are shared
outside the OpenGL backend; OpenGL-specific texture creation and uploads remain
in the OpenGL implementation.

## Build and Test

The PSP Vulkan frontend is built when SDL support is enabled. From the
repository root, configure the Codespace build with CMake and Ninja:

```sh
cmake -S . -B build/vulkan-phase1 -G Ninja \
  -DMHF_WITH_SDL=ON \
  -DMHF_BUILD_TESTS=ON \
  -DMHF_BUILD_LAUNCHER=OFF \
  -DMHF_FFMPEG=system \
  -DCMAKE_CXX_FLAGS="-Wno-error=shadow -Wno-error=missing-field-initializers"
```

The Codespace build uses local warning demotions for the existing `shadow` and
`missing-field-initializers` diagnostics; omit `CMAKE_CXX_FLAGS` where these
workarounds are not needed. This configuration requires CMake 3.28 or newer,
Ninja, Vulkan headers and loader, `glslc`, and system FFmpeg development files.
SDL3 is fetched and built by the project when SDL support is enabled.

Build the Vulkan shaders, PSP frontend, and focused tests:

```sh
cmake --build build/vulkan-phase1 --target \
  mhf_psp_vulkan_shaders mhf_psp_frontend \
  test_psp_vulkan_backend test_texture_cache -j 4
```

Validate the generated Vulkan 1.0 SPIR-V and run the backend/cache tests:

```sh
for shader in build/vulkan-phase1/platforms/psp/shaders/*.spv; do
  spirv-val --target-env vulkan1.0 "$shader"
done

SDL_AUDIODRIVER=dummy XDG_RUNTIME_DIR=/tmp \
  xvfb-run -a ctest --test-dir build/vulkan-phase1 \
  -R 'psp_vulkan_backend|psp_texture_cache' --output-on-failure
```

In the Codespace, these tests ran with Mesa lavapipe (`llvmpipe`) under Xvfb.
The Khronos validation layer is not installed, so those runs did not include
validation-layer diagnostics. `xvfb-run`, `spirv-val`, and `glslc` must be
available for the commands above.
