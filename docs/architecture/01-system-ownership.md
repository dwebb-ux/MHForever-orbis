# System Ownership

Version: 1.0

Status: Foundational

## Purpose

Define ownership boundaries between all major MHForever PS4 subsystems.

Clarify responsibility, dependencies, and authority.

Prevent feature duplication and architecture drift.

---

## Ownership Philosophy

Every system has a single owner.

Every source file should clearly belong to one subsystem.

No subsystem should assume responsibility owned by another.

Ownership must be obvious from directory structure.

---

## Root Ownership Hierarchy

```text
Application

├── Platform Layer
├── Runtime Layer
├── Content Layer
├── Tooling Layer
└── Documentation Layer
```

---

## Platform Layer Responsibilities

Directory:

```text
platforms/
```

Responsible For:

- OS services
- graphics API integration
- audio API integration
- controller APIs
- filesystem APIs
- networking APIs
- process lifecycle

Not Responsible For:

- gameplay
- quest logic
- monster AI
- save format definitions
- asset formats

Components:

```text
platforms/ps4
platforms/windows
platforms/linux
```

---

## Runtime Layer Responsibilities

Directory:

```text
runtime/
```

Responsible For:

- execution runtime
- task scheduling
- memory systems
- rendering abstraction
- audio abstraction
- networking abstraction
- save management
- asset streaming
- serialization
- profiling

Not Responsible For:

- platform APIs
- gameplay rules

---

## Graphics Ownership

Runtime Owns:

```text
IRenderer
Texture Management
Shader Abstraction
Render Commands
Frame Graph
```

Platform Owns:

```text
GNM/GNMX
Vulkan
Direct Rendering APIs
Swapchain Creation
GPU Submission
```

---

## Audio Ownership

Runtime Owns:

```text
Audio Mixer
Channel Management
Stream Scheduling
Playback Logic
```

Platform Owns:

```text
Hardware Audio Output
Backend Initialization
Device Enumeration
```

---

## Input Ownership

Runtime Owns:

```text
Input Actions
Binding System
Profiles
Input Mapping
```

Platform Owns:

```text
DualShock Input
Keyboard Input
Device Polling
Controller Discovery
```

---

## Save Ownership

Runtime Owns:

```text
Save Format
Serialization
Migration
Validation
Versioning
```

Platform Owns:

```text
Storage Access
Directories
Permissions
Persistence
```

---

## Networking Ownership

Runtime Owns:

```text
Packets
Protocol
Sessions
Lobby State
Quest Synchronization
```

Platform Owns:

```text
Sockets
Network Interfaces
Transport Layer
```

---

## Asset Ownership

Runtime Owns:

```text
Asset Import
Stream Management
Caching
Lifetime Control
```

Tools Own:

```text
Extraction
Conversion
Packaging
Compression
```

---

## Tool Ownership

Directory:

```text
tools/
```

Responsible For:

- asset extraction
- build helpers
- package generation
- verification
- converters

Tools never become runtime dependencies.

---

## Documentation Authority

Directory:

```text
docs/
```

Documents define intended architecture.

Source code defines actual implementation.

Disagreements between the two should be resolved immediately.

---

## Dependency Matrix

Allowed:

```text
Game
 ↓
Runtime
 ↓
Platform Interfaces
 ↓
Platform Implementations
```

Forbidden:

```text
Game → PS4 APIs
Gameplay → Renderer Backends
Runtime → Platform Concrete Types
```

---

## Ownership Decision Process

When adding a feature ask:

1. Is this platform specific?
2. Is this runtime functionality?
3. Is this tooling?
4. Is this gameplay data?

Place ownership accordingly.

Never create mixed-responsibility systems.
