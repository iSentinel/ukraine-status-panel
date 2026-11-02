#!/usr/bin/env python3
"""Verify dirty bounds from the production Home map renderer."""

from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    """Compile the renderer test with sanitizers and run its assertions."""
    with TemporaryDirectory(prefix="usp-map-dirty-") as directory:
        binary = Path(directory) / "map-dirty"
        command = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(ROOT / "firmware/include"),
            str(ROOT / "tests/dashboard-map-dirty-area-native.cpp"),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        result = subprocess.run(
            [str(binary)], capture_output=True, text=True, check=True
        )
        logger.info("{}", result.stdout.strip())


if __name__ == "__main__":
    main()
