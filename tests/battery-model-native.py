#!/usr/bin/env python3
"""Validate the pure 1S Li-Po battery estimate and state model."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Compile and run the battery model with sanitizers."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-battery-model-") as directory:
        binary = Path(directory) / "battery-model"
        subprocess.run(
            [
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/battery_host/battery-model.cpp"),
                "-o", str(binary),
            ],
            check=True,
        )
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        result = subprocess.run(
            [str(binary)], check=True, env=env, capture_output=True, text=True
        )
        print(result.stdout.strip())
    logger.info("BATTERY_MODEL_NATIVE=PASS ASAN_UBSAN=clean")


if __name__ == "__main__":
    main()
