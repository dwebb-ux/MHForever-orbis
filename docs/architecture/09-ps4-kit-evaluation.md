# PS4-Kit Integration Evaluation

Status: Recommended, pending toolchain and hardware validation

Evaluated: 2026-11-20

Evaluated PS4-Kit revision:
[`79d173b6b764376945252c0124d595ffca1cab06`](https://github.com/iHaiDeeZ/ps4-kit/commit/79d173b6b764376945252c0124d595ffca1cab06)

## Purpose

Evaluate the integration boundary between MHForever's PS4 implementation and
[iHaiDeeZ/ps4-kit](https://github.com/iHaiDeeZ/ps4-kit). This is a source-level
evaluation, not a claim that the kit has been built or validated with this
repository's Orbis SDK, target hardware, or game workload.

## Decision

**Recommendation: B — use PS4-Kit selectively as a dependency of the PS4
backend.**

PS4-Kit contains both platform/runtime components and build/package tools. It
is therefore broader than a tooling-only dependency, but it should not replace
MHForever's platform layer. Keep `platforms/ps4/` as the owner of the
`ILogger`, `IFileSystem`, input, renderer, and `RuntimeBootstrap` adapters.
Integrate only kit components that pass the compatibility and behavior gates
below, behind those existing boundaries.

This recommendation is conditional: until toolchain, link, and on-device
checks pass, PS4-Kit is an evaluation target and not a required dependency.
Do not block platform-independent runtime work on the evaluation.

### Why not A — the PS4 platform backend

- The repository owns the MHForever interfaces and platform adapter boundary;
  PS4-Kit does not provide `mhf::runtime` service implementations or the
  `RuntimeContext` registration policy.
- `ps4_kit_app()` brings in runtime objects, linker wraps, system libraries,
  and EBOOT generation. Adopting it wholesale could overlap with current
  backend implementations and the existing Orbis toolchain setup.
- Kit's Vulkan WSI is a presentation layer for its statically linked RADV. It
  does not, by itself, implement MHForever's `IRenderer`.

### Why not C — tooling-only dependency

- Alongside packaging scripts, the kit provides runtime diagnostics, file and
  process-runtime support, and a Vulkan/VideoOut presentation implementation.
- Those components could provide backend capabilities that cannot be
  represented as packaging-only tooling, subject to the gates below.

## Current MHForever Boundary

`platforms/ps4/` currently builds `mhf_platform_ps4` with the project
Orbis toolchain and links it to `mhf_runtime`. It provides `Ps4Logger`,
`Ps4FileSystem`, and `Ps4Input`; the PS4 bootstrap registers those services.
There is not yet a PS4 renderer implementation. Packaging and asset
validation live in project tooling and documentation. These are the boundaries
to preserve during evaluation.

## Integration Points

| Area | PS4-Kit capability | MHForever integration and evaluation |
|---|---|---|
| `ILogger` | The kit's boot trace and diagnostics provide early startup and failure reporting. Its public API is in `runtime/ps4_kit_diag.h`; it is not an `ILogger` implementation. | Keep `Ps4Logger` as the `ILogger` adapter. Evaluate forwarding or correlating platform diagnostics with the kit boot trace without changing the runtime interface. Check stderr/linker-wrap interactions, log ordering, and behavior before and after the diagnostics subsystem initializes. |
| `IFileSystem` | Kit runtime includes a file layer with `*at` operations, current-directory and path handling, and optional network filesystems. | Keep the `IFileSystem` contract authoritative and `Ps4FileSystem` as its adapter. Compare path containment, error reporting, directory behavior, and file semantics. Adopt file-layer primitives only if the adapter preserves contract behavior and does not conflict with kit linker wraps or diagnostics' open-file tracking. The current interface is not a general file-handle API. |
| Packaging | `ps4_kit_add_eboot()` creates and checks an EBOOT; `scripts/make-pkg.sh` stages and builds a package using external OpenOrbis/PkgTool utilities. | Evaluate the EBOOT and package steps separately against the existing toolchain and `tools/mhf_asset_builder.py`. Reuse or invoke packaging tools explicitly; retain MHF validation, caller-owned asset inputs, and reproducible staging. Never place signing credentials or user game data in the repository. |
| Diagnostics | Crash logging, boot traces, thread dumps, sampling, profiling, and host collection/symbolization tools are included. | Highest-value candidate for selective adoption. Check signal/constructor behavior, thread hooks, memory cost, generated/JIT code reporting hooks, data-directory policy, and compatibility with MHF logging and file access. Keep diagnostics optional and configure app-specific hooks only where available. |
| Renderer backend | `gpu/ps4_vk_wsi.*` adds Vulkan surface/swapchain presentation on VideoOut for the kit's statically linked RADV build. | Evaluate only if Vulkan/RADV is selected for MHF. WSI supplies presentation, not an `IRenderer`; MHF still needs a backend that owns resource creation, command submission, synchronization, and the `IRenderer` implementation. Compare with the planned PS4 graphics path and verify formats, frame pacing, memory use, and hardware behavior. |
| Runtime bootstrap | `ps4_kit_app()` links runtime objects, linker wraps, system libraries, and EBOOT generation. The kit runtime also supplies process-level facilities such as heap/runtime hooks. | Do not replace `RuntimeBootstrap` or `RuntimeContext`. Map kit process initialization and shutdown requirements around MHF platform startup, identify constructors and wrapped symbols, and test link/runtime conflicts. Register only successfully constructed MHF services through the existing bootstrap. |

## Evaluation Plan

1. **Pin and inspect.** Keep the evaluated revision recorded in this document.
   Identify which kit files and build functions each proposed integration
   needs; do not copy the repository wholesale.
2. **Check toolchain compatibility.** Compare the kit's Clang/libc++ toolchain,
   sysroot, compiler flags, linker script, and CMake hooks with
   `cmake/toolchains/orbis.cmake` and the configured Orbis SDK. Confirm that
   both targets use a single compatible compiler and sysroot.
3. **Check link ownership.** Enumerate linker wraps, startup objects,
   constructors, system-library links, and EBOOT commands from both projects.
   Resolve duplicate or conflicting definitions before enabling any kit
   runtime target.
4. **Prototype one boundary at a time.** Start with diagnostics and packaging.
   Evaluate file-layer reuse only after contract tests are available. Evaluate
   Vulkan WSI only after choosing Vulkan/RADV as the renderer direction.
5. **Validate on target.** Build and launch a minimal authorized test
   application, then exercise startup, logging, filesystem operations,
   crash-report retrieval, frame presentation (if selected), and package
   installation. Desktop compilation alone is not acceptance.
6. **Record the decision.** Confirm B, narrow adoption to C, or reject
   integration based on the compatibility and validation results. Update the
   platform build and this report only after the decision is supported by
   evidence.

## Acceptance Gates

- No PS4-Kit headers, APIs, or types leak into platform-independent runtime
  interfaces or gameplay.
- `RuntimeContext` still contains only working MHF service implementations;
  kit support does not introduce placeholders or null services.
- Exactly one compatible PS4 toolchain, sysroot, startup/runtime path, and
  owner for each linker wrap is used.
- Existing MHF platform behavior and filesystem/security constraints are
  preserved by focused tests.
- Any adopted diagnostics or renderer facility has measured memory and
  thread-lifecycle behavior on target hardware.
- Packaging uses explicit staged inputs and passes MHF asset validation.
- The selected PS4-Kit revision and any required component/license notices
  remain traceable.

## Source References

The evaluation is based on the PS4-Kit README and the following files at the
recorded revision:

- [`ps4-kit.cmake`](https://github.com/iHaiDeeZ/ps4-kit/blob/79d173b6b764376945252c0124d595ffca1cab06/ps4-kit.cmake)
- [`runtime/ps4_kit_diag.h`](https://github.com/iHaiDeeZ/ps4-kit/blob/79d173b6b764376945252c0124d595ffca1cab06/runtime/ps4_kit_diag.h)
- [`runtime/ps4_kit_io.h`](https://github.com/iHaiDeeZ/ps4-kit/blob/79d173b6b764376945252c0124d595ffca1cab06/runtime/ps4_kit_io.h)
- [`gpu/ps4_vk_wsi.h`](https://github.com/iHaiDeeZ/ps4-kit/blob/79d173b6b764376945252c0124d595ffca1cab06/gpu/ps4_vk_wsi.h)
- [`scripts/make-pkg.sh`](https://github.com/iHaiDeeZ/ps4-kit/blob/79d173b6b764376945252c0124d595ffca1cab06/scripts/make-pkg.sh)
- [`docs/merge-notes/diagnostics.md`](https://github.com/iHaiDeeZ/ps4-kit/blob/79d173b6b764376945252c0124d595ffca1cab06/docs/merge-notes/diagnostics.md)

