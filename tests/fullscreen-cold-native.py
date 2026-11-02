"""Test real cold rendering/cache ownership with controlled platform boundaries."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "tests/fullscreen_cache_host"


def compile_fixture(name, binary):
    """Compile the production headers and generated runs with ASAN/UBSAN."""
    command = [
        "g++",
        "-std=c++17",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(HOST / "stubs"),
        "-I",
        str(ROOT / "include"),
        str(HOST / name),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True)


def main():
    """Verify budgets/pixels/lifetime/fallback; do not claim hardware timing."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-fullscreen-cold-") as directory:
        for fixture, variants in (
            ("renderer-cold.cpp", ([],)),
            ("cache-boundary.cpp", ([], ["allocation-failure"])),
        ):
            binary = Path(directory) / Path(fixture).stem
            compile_fixture(fixture, binary)
            for arguments in variants:
                subprocess.run([str(binary)] + arguments, check=True)
    logger.info(
        "FULLSCREEN_COLD_NATIVE=PASS pixels/budget/ownership/fallback; platform controlled"
    )


if __name__ == "__main__":
    main()
