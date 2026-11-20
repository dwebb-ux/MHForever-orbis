# PS4 Port Strategy

## Objective

Execute MHForever as a native PlayStation 4 application using
the existing static recompilation architecture.

## Core Principle

Do not port gameplay.

Port the runtime.

The recompiled game should remain platform agnostic.

## Platform Responsibilities

### PS4 Layer

- Graphics
- Audio
- Input
- Save Data
- Networking
- Packaging

### Core Runtime

- Game execution
- Asset loading
- Quest systems
- Monster AI
- Combat
- Serialization

## Target Outcome

The same recompiled game binary should execute against:

- Windows Runtime
- Linux Runtime
- PS4 Runtime

with no game-specific source modifications.
