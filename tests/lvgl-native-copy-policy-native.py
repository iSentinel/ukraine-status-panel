#!/usr/bin/env python3
"""Verify the native framebuffer DMA copy admission policy."""

from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    """Compile the standalone production policy with sanitizers."""
    with TemporaryDirectory(prefix="usp-native-copy-") as directory:
        binary = Path(directory) / "native-copy-policy"
        command = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(ROOT / "components" / "lvgl"),
            str(ROOT / "tests" / "lvgl-native-copy-policy-native.cpp"),
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
