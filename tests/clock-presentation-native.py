#!/usr/bin/env python3
"""Check calendar truth and minute updates without changing device time."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Compile the real portable formatter with memory and undefined checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise FileNotFoundError(args.compiler)
    with TemporaryDirectory(prefix="usp-clock-") as directory:
        binary = Path(directory) / "clock"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-g", "-I", str(ROOT / "include"),
            str(ROOT / "tests/clock-presentation-native.cpp"), "-o",
            str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    logger.info("CLOCK_MODEL=PASS device_time_changed=0")


if __name__ == "__main__":
    main()
