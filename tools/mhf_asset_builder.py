#!/usr/bin/env python3
"""Validate and build MHForever PS4 runtime assets.

This tool never downloads or copies copyrighted game assets.  It accepts a
caller-provided package data directory and validates the package layout before
optionally invoking an external PS4 SDK packaging tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


FORBIDDEN_SUFFIXES = {
    ".iso", ".cso", ".zso", ".ciso", ".chd", ".pak", ".pkg", ".elf",
    ".prx", ".self", ".pbp", ".ecm", ".bin",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_tree(root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    if not root.is_dir():
        return [f"asset root is not a directory: {root}"]

    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() in FORBIDDEN_SUFFIXES:
            errors.append(f"copyrighted or executable asset refused: {path}")
        if any(part in {".git", "__pycache__", "build", "generated"} for part in path.parts):
            errors.append(f"non-distributable path refused: {path}")
    return errors


def write_manifest(root: pathlib.Path, manifest_path: pathlib.Path) -> None:
    files: list[dict[str, object]] = []
    for path in sorted(root.rglob("*")):
        if path.is_file():
            files.append({
                "path": path.relative_to(root).as_posix(),
                "size": path.stat().st_size,
                "sha256": sha256(path),
            })

    manifest = {
        "format": "mhf-asset-manifest",
        "version": 1,
        "file_count": len(files),
        "files": files,
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_package(package_tool: pathlib.Path, manifest: pathlib.Path,
                output: pathlib.Path, eboot: pathlib.Path,
                param_sfo: pathlib.Path) -> int:
    if not package_tool.is_file():
        print(f"error: package tool not found: {package_tool}", file=sys.stderr)
        return 2
    if not eboot.is_file():
        print(f"error: EBOOT not found: {eboot}", file=sys.stderr)
        return 2
    if not param_sfo.is_file():
        print(f"error: PARAM.SFO not found: {param_sfo}", file=sys.stderr)
        return 2

    command = [
        str(package_tool),
        "--manifest", str(manifest),
        "--eboot", str(eboot),
        "--param-sfo", str(param_sfo),
        "--output", str(output),
    ]
    result = subprocess.run(command, check=False)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path, help="caller-owned asset package directory")
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--eboot", type=pathlib.Path)
    parser.add_argument("--param-sfo", type=pathlib.Path)
    parser.add_argument("--package-tool", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    errors = validate_tree(args.root)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 2

    write_manifest(args.root, args.manifest)
    print(f"validated {args.root}: wrote {args.manifest}")

    if args.package_tool is not None:
        missing = [name for name, value in (
            ("--eboot", args.eboot),
            ("--param-sfo", args.param_sfo),
            ("--output", args.output),
        ) if value is None]
        if missing:
            print(f"error: package mode requires {' '.join(missing)}", file=sys.stderr)
            return 2
        assert args.eboot is not None
        assert args.param_sfo is not None
        assert args.output is not None
        return run_package(args.package_tool, args.manifest, args.output,
                           args.eboot, args.param_sfo)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
