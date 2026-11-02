#!/usr/bin/env python3
"""Run real restart-confirmation logic without restarting a physical panel."""
import argparse
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Compile boundary and rollover cases with memory checks enabled."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    with TemporaryDirectory(prefix="usp-confirmation-") as temporary:
        output = Path(temporary) / "confirmation"
        subprocess.run(
            [
                args.compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-fno-pie",
                "-no-pie",
                "-I",
                str(ROOT / "include"),
                str(ROOT / "tests/confirmation-native.cpp"),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)
    logger.info("Physical restart not performed")


if __name__ == "__main__":
    main()
