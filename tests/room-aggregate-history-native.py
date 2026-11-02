#!/usr/bin/env python3
"""Test the real aggregate room-history backend without panel access."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def compile_and_run(compiler: str, directory: Path) -> None:
    """Compile actual production C++ with isolated host boundaries."""
    target = directory / "room-aggregate-history"
    command = [
        compiler,
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined,float-cast-overflow",
        "-fno-omit-frame-pointer",
        "-g",
        "-I",
        str(ROOT / "tests/room_aggregate_history_host/shims"),
        "-I",
        str(ROOT / "components/usp_room_aggregate_history"),
        str(ROOT / "tests/room-aggregate-history-native.cpp"),
        str(ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.cpp"),
        "-o",
        str(target),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(target)], check=True)


def main() -> None:
    """Run ASAN/UBSAN against the exact component sources."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("A C++20 compiler is required")
    with TemporaryDirectory(prefix="usp-room-aggregate-") as temporary:
        compile_and_run(compiler, Path(temporary))
    logger.info("ROOM_AGGREGATE_NATIVE_RUN=PASS no_device=1")


if __name__ == "__main__":
    main()
