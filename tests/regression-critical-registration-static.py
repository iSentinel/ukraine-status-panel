#!/usr/bin/env python3
"""Require the reviewed offline checks to remain in the regression runner."""
import argparse
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    "battery-support-static.py",
    "battery-model-native.py",
    "web-auth-rejection-native.py",
    "digest-session-native.py",
    "room-cursor-migration-native.py",
    "room-clock-recovery-native.py",
    "room-clock-recovery-static.py",
    "nvs-startup-preserve-static.py",
    "nvs-startup-preserve-native.py",
    "room-period-chart-native.py",
    "room-period-text-native.py",
    "room-period-ui-static.py",
    "restart-confirm-text-catalog-native.py",
    "shelly-em50-static.py",
    "work-checkpoint-native.py",
    "regression-critical-registration-static.py",
)


def main():
    """Check executable run entries, not names mentioned in comments."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    runner = (ROOT / "scripts/run-regression.sh").read_text()
    entries = re.findall(r"(?m)^run ([A-Za-z0-9_.-]+)[ \t]*$", runner)
    for name in REQUIRED:
        assert (ROOT / "tests" / name).is_file(), name
        assert entries.count(name) == 1, (name, entries.count(name))
    logger.info("REGRESSION_CRITICAL_REGISTRATION=PASS checks={}", len(REQUIRED))


if __name__ == "__main__":
    main()
