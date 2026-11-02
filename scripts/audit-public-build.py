#!/usr/bin/env python3
"""Check public firmware inputs/artifacts for values from a private secrets file."""
from __future__ import annotations

import argparse
import base64
from pathlib import Path

import yaml


def secret_needles(path: Path) -> list[tuple[str, bytes]]:
    """Return literal and decoded binary needles without exposing their values."""
    values = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    needles: list[tuple[str, bytes]] = []
    for name, value in values.items():
        if value is None:
            continue
        raw = str(value).encode()
        if len(raw) >= 4:
            needles.append((f"{name}:literal", raw))
        try:
            decoded = base64.b64decode(str(value), validate=True)
        except Exception:
            continue
        if len(decoded) >= 16:
            needles.append((f"{name}:base64-decoded", decoded))
    return needles


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-secrets", required=True, type=Path)
    parser.add_argument("artifact", nargs="+", type=Path)
    args = parser.parse_args()
    needles = secret_needles(args.private_secrets)
    failures: list[tuple[Path, str]] = []
    for artifact in args.artifact:
        blob = artifact.read_bytes()
        for label, needle in needles:
            if needle in blob:
                failures.append((artifact, label))
    if failures:
        for artifact, label in failures:
            print(f"ERROR: private value match in {artifact.name}: {label}")
        return 1
    print(f"Public credential audit: PASS ({len(args.artifact)} artifacts, {len(needles)} needles)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
