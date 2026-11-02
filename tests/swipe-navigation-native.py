#!/usr/bin/env python3
"""Run bounded swipe/tap/escape checks with the real portable gesture model."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def arguments():
    """Allow a compiler override without contacting any device."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    return parser.parse_args()


def main(args):
    """Build and execute the gesture model with memory and undefined checks."""
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError(f"Compiler not found: {args.compiler}")
    with tempfile.TemporaryDirectory(prefix="usp-swipe-") as directory:
        binary = Path(directory) / "swipe-native"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-g",
            "-I",
            str(ROOT / "include"),
            str(ROOT / "tests/swipe-navigation-native.cpp"),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    logger.info(
        "SWIPE_MODEL=PASS; no network, touchscreen or preferences used"
    )


if __name__ == "__main__":
    main(arguments())
