# FPKG Build Pipeline

Version: 1.0

Status: Foundational

## Objective

Provide deterministic packaging and deployment of MHForever PS4.

---

## Pipeline Philosophy

Packaging is reproducible.

Packaging is automated.

Packaging is validated.

No manual editing.

---

## Pipeline Stages

```text
Source

↓

Compile

↓

Link

↓

Asset Verification

↓

EBOOT Generation

↓

Package Generation

↓

Validation

↓

Release Artifact
```

---

## Inputs

Source:

```text
runtime/
platforms/
game/
tools/
```

Generated Assets:

```text
generated/
```

User Assets:

Not stored in the repository.

---

## Build Outputs

Primary Output:

```text
MHForever-PS4.pkg
```

Secondary Outputs:

```text
Logs
Symbols
Build Reports
```

---

## Package Layout

```text
sce_sys/
eboot.bin
data/
```

---

## Asset Build Pipeline

Desktop Tool:

```text
MHFAssetBuilder
```

Responsibilities:

- extraction
- conversion
- verification
- packaging

Output:

```text
pkgdata/
```

---

## Validation

Verify:

- required assets
- hashes
- archive integrity
- package structure

Build must fail on validation errors.

---

## Build Automation

Preferred:

```text
CMake
```

Support:

```text
Debug
Development
Release
```

---

## Release Requirements

Before package generation:

- builds clean
- tests pass
- validation passes
- no missing assets

---

## CI/CD Goals

Future automation:

```text
Push

↓

Build

↓

Test

↓

Package

↓

Validate

↓

Release Artifact
```

---

## Future Expansion

Pipeline should support:

- nightly builds
- PS5 packaging
- multiplayer test builds
- automated regression testing

without redesign.
