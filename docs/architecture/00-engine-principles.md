# Engine Principles

Version: 1.0

Status: Foundational

This document defines the architectural rules governing
MHForever PS4.

All technical decisions should be evaluated against these principles.

---

# Principle 1

Runtime First

The runtime is the product.

Gameplay is data executed by the runtime.

The runtime should provide:

- Execution services
- Rendering services
- Audio services
- Input services
- Filesystem services
- Networking services

Gameplay should consume services.

Gameplay should never provide services.

---

# Principle 2

Port The Runtime, Not The Game

The objective is platform portability.

We do not rewrite Monster Hunter systems.

We execute them.

Success looks like:

- Windows Runtime
- Linux Runtime
- PS4 Runtime

Running identical game logic.

If platform work requires gameplay modifications,
architecture should be reconsidered.

---

# Principle 3

Platform Isolation

Every platform implementation must be isolated.

Examples:

- `platforms/ps4/`
- `platforms/windows/`
- `platforms/linux/`

Only platform code may directly access:

- graphics APIs
- operating system APIs
- controller APIs
- networking APIs

The remainder of the codebase should remain platform agnostic.

---

# Principle 4

Replaceability

Every system should be replaceable.

Required examples:

- `IRenderer`
- `IAudioSystem`
- `IInputSystem`
- `INetworkSystem`
- `IFileSystem`

Concrete implementations should remain hidden.

Subsystem replacement should not require gameplay changes.

---

# Principle 5

Deterministic Behavior

The runtime should strive for deterministic execution.

Benefits:

- easier debugging
- networking consistency
- replay systems
- save validation

Avoid dependency on:

- wall-clock timing
- platform-specific scheduler behavior
- undefined execution order

---

# Principle 6

Performance Before Convenience

Target:

60 FPS gameplay

Frame Budget:

16.67 ms

Every allocation, synchronization primitive,
and API call carries cost.

Convenient code that risks frame pacing should be rejected.

---

# Principle 7

No Per-Frame Allocation

Hot-path allocations are prohibited.

Examples:

- Render Loop
- Audio Mix Loop
- Gameplay Update Loop
- Network Tick

Use:

- pools
- arenas
- fixed buffers
- frame allocators

Memory behavior must be predictable.

---

# Principle 8

Data-Oriented Thinking

Prioritize memory locality.

Prefer:

- contiguous data
- batch processing
- cache-friendly iteration

Avoid:

- deep object hierarchies
- pointer chasing
- fragmented ownership

---

# Principle 9

Ownership Must Be Explicit

Every subsystem must have clear ownership.

Questions that should always have answers:

- Who creates this object?
- Who owns it?
- Who destroys it?
- When does it become invalid?

Implicit ownership is forbidden.

---

# Principle 10

Failure Must Be Visible

Silent failure is unacceptable.

Every failure should be:

- detectable
- loggable
- diagnosable

Systems should fail predictably.

Unexpected termination is preferable to silent corruption.

---

# Principle 11

Asset Independence

No copyrighted game assets shall exist in the repository.

Repository contents may include:

- extraction tools
- build tools
- converters

Repository contents may not include:

- game textures
- models
- sounds
- movies
- executable assets

Users provide their own content.

---

# Principle 12

Multiplayer Ready Architecture

Networking should not be bolted on later.

Systems should be designed assuming:

- future synchronization
- future multiplayer sessions
- future authoritative state management

Single-player shortcuts that block multiplayer
development should be avoided.

---

# Principle 13

Cross Platform By Default

New systems should assume multiple targets.

Supported targets:

- Windows
- Linux
- PlayStation 4

Future targets:

- Steam Deck
- PS5
- ARM Linux

Platform-specific assumptions should remain isolated.

---

# Principle 14

Thread Safety Is Intentional

Concurrency must be designed,
not discovered accidentally.

Shared mutable state should be minimized.

Every subsystem should document:

- ownership
- synchronization
- update model

---

# Principle 15

Documentation Is Architecture

If a system exists,
it should be documented.

Required documentation:

- Purpose
- Responsibilities
- Dependencies
- Threading
- Memory Usage
- Failure Modes
- Future Work

Architecture lives in documents,
not only source code.

---

# Principle 16

Build Reproducibility

Any contributor should be able to:

- clone
- configure
- build
- run

using documented procedures.

Build success must not depend on:

- local hacks
- undocumented environment variables
- manual code modifications

---

# Principle 17

Incremental Development

Large changes should be decomposed.

Preferred order:

1. Interface
2. Core Logic
3. Platform Backend
4. Testing
5. Documentation

Massive single-step rewrites should be avoided.

---

# Principle 18

Measure Before Optimizing

Use profiling data whenever possible.

Priority metrics:

- Frame Time
- Memory Usage
- Loading Time
- Network Latency
- Asset Streaming Performance

Optimization decisions should be evidence based.

---

# Principle 19

Long-Term Maintainability

The codebase should remain understandable years later.

Favor:

- clarity
- consistency
- predictability

over cleverness.

Future contributors should be able to understand intent quickly.

---

# Principle 20

The Runtime Is Forever

Games may change.

Platforms may change.

Renderers may change.

The runtime architecture should survive them all.

Every design decision should strengthen:

- portability
- maintainability
- performance
- extensibility

over short-term implementation speed.
