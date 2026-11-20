# Threading Model

Version: 1.0

Status: Foundational

## Goal

Maintain stable 60 FPS gameplay.

Optimize for:

- frame consistency
- safety
- scalability

---

## Core Threads

### Main Thread

Responsibilities:

- game update
- quest logic
- AI
- save requests
- command generation

Target:

≤4 ms

---

### Render Thread

Responsibilities:

- command submission
- frame execution
- presentation

Target:

≤8 ms

---

### Asset Streaming Thread

Responsibilities:

- texture loading
- archive access
- decompression

Target:

asynchronous

Must never block gameplay.

---

### Audio Thread

Responsibilities:

- channel updates
- mixer processing
- streaming

Target:

≤1 ms

---

### Networking Thread

Responsibilities:

- socket IO
- packet parsing
- queue processing

Must operate independently from the render thread.

---

## Communication Principles

Allowed:

```text
Lock-Free Queues
Double Buffering
Message Passing
Job Dispatching
```

Avoid:

```text
Global Locks
Cross-thread Ownership
Shared Mutable State
```

---

## Render Communication

Main Thread:

```text
Build Frame Commands
```

Render Thread:

```text
Execute Commands
```

The render thread never reads live gameplay state.

---

## Asset Streaming Flow

```text
Request Asset

↓

Streaming Queue

↓

Background Load

↓

GPU Upload Queue

↓

Renderer
```

---

## Thread Safety Rules

Every subsystem documents:

- owner thread
- access rules
- synchronization model

Ownership must be singular.

---

## Future Expansion

Support:

- additional worker threads
- task graph execution
- job system integration

without gameplay changes.
