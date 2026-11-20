# MHForever PS4 Roadmap

## Phase 0

Repository Fork

Status: In Progress

Goals

- Establish PS4 branch
- Create platform abstraction
- Create documentation
- Define memory budgets

Deliverables

- Buildable repository
- Architecture documentation

---

## Phase 1

Platform Bring-Up

Goals

- ELF booting
- Controller support
- Display initialization
- Filesystem mounting

Deliverables

- Black screen → rendered frame

Success Criteria

- Stable application launch

---

## Phase 2

Renderer

Markdown

Goals

- Rendering backend
- Texture uploads
- Frame presentation
- UI rendering

Milestones

- Splash screen
- Main menu
- Village rendering

Success Criteria

- 60 FPS menu

---

## Phase 3

Runtime Integration

Markdown

Goals

- PSP syscall replacement
- Asset loading
- Save loading
- Input translation

Milestones

- **Milestone 3 — Evaluate PS4-Kit integration points:** assess `ILogger`,
  `IFileSystem`, packaging, diagnostics, renderer backend, and runtime bootstrap.
  Decide whether PS4-Kit is the PS4 platform backend, a dependency of that
  backend, or a tooling-only dependency. Record evidence and compatibility
  gates in [09-ps4-kit-evaluation.md](architecture/09-ps4-kit-evaluation.md).
  This evaluation must not block runtime work or make unverified kit
  components required.

Success Criteria

- Character loads
- Save compatibility

---

## Phase 4

Gameplay Validation

Markdown

Goals

- Hub areas
- Quests
- Combat
- AI

Success Criteria

- Complete hunt

---

## Phase 5

Performance

Markdown

Goals

- Memory optimization
- Draw call reduction
- Asset streaming
- Cache systems

Performance Targets

- 60 FPS: Village
- 60 FPS: Combat
- No crashes: 4+ hour sessions

---

## Phase 6

Packaging

Markdown

Goals

- EBOOT generation
- Param.sfo
- PKG generation
- Installer process

Success Criteria

- Single-click install

---

## Phase 7

Online Support

Markdown

Goals

- Socket layer
- Lobby support
- Quest synchronization

Success Criteria

- Four-player hunt
