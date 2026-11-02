#!/usr/bin/env python3
"""Verify provider-local weather time formatting and legacy parser equivalence."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Run the real pure helper under ASAN/UBSAN without any device access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-weather-time-") as directory:
        binary = Path(directory) / "weather-time"
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "include"),
                str(ROOT / "tests/weather_host/weather-time.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        subprocess.run([str(binary)], check=True, env=environment)
    logger.info("WEATHER_TIME_NATIVE=PASS source-local time; hardware not used")


if __name__ == "__main__":
    main()
