#!/usr/bin/env python3
"""Guard generic hardware/live probes against stale firmware-version assumptions."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def is_probe(path: Path) -> bool:
    name = path.name
    return any(token in name for token in ("live", "hardware", "probe"))


def main() -> int:
    checked = 0
    errors: list[str] = []
    hardcoded = re.compile(
        r"""project_version\s*(?:==|!=)\s*["']0\.115\.\d+-dev["']"""
    )
    malformed = re.compile(r"getattr\(\s*info\s*,\s*project_version\b")

    for path in sorted(ROOT.glob("*.py")):
        if not is_probe(path):
            continue
        text = path.read_text(encoding="utf-8")
        if "build_client(" not in text:
            continue
        checked += 1
        if hardcoded.search(text):
            errors.append(f"{path.name}: hardcoded project_version comparison")
        if malformed.search(text):
            errors.append(f"{path.name}: unquoted project_version getattr")
        verify_pos = text.find("verify_device(")
        expected_pos = text.find("expected_version(CONFIG)")
        if verify_pos < 0 and expected_pos < 0:
            errors.append(f"{path.name}: no YAML-derived firmware version guard")


    if errors:
        for error in errors:
            print(f"LIVE_PROBE_VERSION_SAFETY=FAIL {error}")
        return 1
    print(f"LIVE_PROBE_VERSION_SAFETY=PASS checked={checked} hardcoded_dev=0 yaml_guard=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
