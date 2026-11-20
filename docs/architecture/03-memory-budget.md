# Memory Budget

Version: 1.0

Status: Foundational

## Objective

Create predictable PS4 memory behavior.

Avoid runtime allocation spikes.

---

## Total Budget

Target RAM:

5 GB

Reserve:

1 GB

Usable Runtime Budget:

4 GB

---

## Recommended Allocation

### Runtime

512 MB

Contains:

- scheduler
- save systems
- networking
- runtime state

---

### Graphics

1024 MB

Contains:

- textures
- framebuffers
- render targets

---

### Streaming Cache

1024 MB

Contains:

- loaded archives
- decompressed assets

---

### Gameplay

512 MB

Contains:

- entities
- quest state
- AI

---

### Audio

256 MB

Contains:

- music
- effects
- streams

---

### Safety Margin

768 MB

Reserved.

Never intentionally consume.

---

## Allocation Rules

Avoid:

```text
new
delete
malloc
free
```

in hot paths.

Prefer:

```text
Arena Allocators
Object Pools
Frame Allocators
Fixed Buffers
```

---

## Memory Categories

### Persistent

Life:

Application

Examples:

- Renderer
- Managers

---

### Session

Life:

Gameplay Session

Examples:

- Quest State

---

### Level

Life:

Map

Examples:

- Textures
- Audio
- Models

---

### Frame

Life:

Single Frame

Reset Every Frame

---

## Monitoring

Collect:

- total memory
- allocation count
- high-water mark
- fragmentation

Expose via diagnostics overlay.

---

## Failure Behavior

Never crash silently.

On OOM:

1. log failure
2. dump statistics
3. attempt recovery
4. terminate safely
