#!/usr/bin/env python3
"""Check that ESPHome copies every project header needed by a registered header."""
import argparse
from pathlib import Path
import re
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_profile(profile):
    """Reject missing direct or transitive includes before the firmware build."""
    text = profile.read_text()
    block = text.split("  includes:\n", 1)[1].split("\n\n", 1)[0]
    names = set(re.findall(r"    - include/(usp_[^\n]+\.h)", block))
    assert names, profile
    for name in sorted(names):
        path = ROOT / "include" / name
        assert path.is_file(), path
        needed = set(re.findall(r'#include "(usp_[^"]+\.h)"', path.read_text()))
        missing = needed - names
        assert not missing, f"{profile.name}: {name} needs {sorted(missing)}"
    logger.info("PROJECT_HEADER_CLOSURE=PASS profile={} headers={}",
                profile.name, len(names))


def main():
    """Validate private and portable profiles without compiling or deploying."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    for name in ("ukraine-status-panel.yaml",
                 "ukraine-status-panel-public-first-flash.yaml"):
        check_profile(ROOT / "firmware" / name)


if __name__ == "__main__":
    main()
