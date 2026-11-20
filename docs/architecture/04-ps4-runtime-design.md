# PS4 Runtime Design

Version: 1.0

Status: Foundational

## Goal

Execute MHForever's recompiled game runtime on PS4 through a clean platform abstraction.

---

## Fundamental Rule

Do not port gameplay.

Port services.

---

## Runtime Architecture

```text
Game Logic

↓

Runtime Services

↓

Platform Interfaces

↓

PS4 Backend
```

---

## PS4 Responsibilities

### Graphics

Initialize:

- video output
- command buffers
- swapchain

Manage:

- presentation
- GPU synchronization

---

### Audio

Initialize audio output.

Manage:

- device communication
- audio buffers

---

### Input

Support:

- DualShock 4
- future keyboard support

Translate hardware input into runtime actions.

---

### Filesystem

Provide:

```text
/data/mhforever/
```

Structure:

```text
saves/
cache/
generated/
textures/
logs/
```

---

### Networking

Provide:

- socket transport
- session connectivity
- packet send/receive

Protocol remains runtime-owned.

---

## Runtime Services

Remain platform independent.

Include:

- execution runtime
- task scheduling
- networking protocol
- save management
- renderer abstraction

---

## Boot Process

```text
Application Start

↓

Platform Initialization

↓

Runtime Initialization

↓

Asset Validation

↓

Save Validation

↓

Renderer Startup

↓

Main Menu
```

---

## Shutdown Process

```text
Gameplay Exit

↓

Resource Flush

↓

Save Completion

↓

Renderer Shutdown

↓

Platform Shutdown
```

---

## Suspend/Resume

Must support:

```text
Suspend

↓

Resource Preservation

↓

Resume

↓

Validation

↓

Continue
```

---

## Long-Term Goal

Future PS5 support should require replacing only platform backend code.

Runtime should remain unchanged.
