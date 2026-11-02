#!/usr/bin/env python3
"""Reject static tests pinned to a specific current development version."""

import re
from pathlib import Path

from firmware_version_guard import canonical_firmware_version


ROOT = Path(__file__).resolve().parents[1]
PIN = re.compile(r'firmware_version:\s*"0\.115\.\d+-dev"')


def pinned_files() -> list[str]:
    """Return test scripts containing a literal current-version marker."""
    matches = []
    for path in sorted((ROOT / "tests").glob("*.py")):
        if path.name == Path(__file__).name:
            continue
        if PIN.search(path.read_text()):
            matches.append(path.name)
    return matches


def main() -> None:
    """Fail closed when a static test would break on a version-only bump."""
    pinned = pinned_files()
    if pinned:
        raise AssertionError(f"hardcoded firmware versions: {pinned}")
    version = canonical_firmware_version()
    print(f"STATIC_TEST_VERSION_SAFETY=PASS version={version} pinned=0")


if __name__ == "__main__":
    main()
