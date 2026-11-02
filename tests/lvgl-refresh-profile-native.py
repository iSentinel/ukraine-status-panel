#!/usr/bin/env python3
"""Compile and test the exact profiler arithmetic used by the firmware."""

import shutil
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Run native boundary checks; a missing compiler is a failed gate."""
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("Install g++ to run the profiler boundary gate")
    with tempfile.TemporaryDirectory(prefix="usp-profile-") as directory:
        binary = Path(directory) / "profile-test"
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "components/lvgl"),
                str(ROOT / "tests/lvgl-refresh-profile-native.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    logger.info("Firmware profiler boundary checks passed")


if __name__ == "__main__":
    main()
