#!/usr/bin/env python3
"""Test the real room-history component without changing the physical panel."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def compile_and_run(compiler, directory, probe):
    """Use memory checks and isolated allocation failures around real C++."""
    target = directory / "room-history"
    command = [
        compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined,float-cast-overflow",
        "-fno-omit-frame-pointer", "-g",
        "-I", str(ROOT / "tests/room_history_host/shims"),
        "-I", str(ROOT / "components/usp_room_history"),
        str(ROOT / "tests/room-history-native.cpp"),
        str(ROOT / "components/usp_room_history/room_history.cpp"),
        "-o", str(target),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(target)] + (["--probe"] if probe else []), check=True)


def main():
    """Expose an observation-only mode for before/after conversion evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument(
        "--probe", action="store_true",
        help="Print current conversion behavior instead of regression checks",
    )
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("A C++20 compiler is required")
    with TemporaryDirectory(prefix="usp-room-history-") as temporary:
        compile_and_run(compiler, Path(temporary), args.probe)
    logger.info("ROOM_HISTORY_NATIVE_RUN=PASS probe={}", args.probe)


if __name__ == "__main__":
    main()
