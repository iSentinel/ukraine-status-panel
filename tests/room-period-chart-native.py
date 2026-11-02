#!/usr/bin/env python3
"""Test the real Room aggregate-period chart model without panel access."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def compile_and_run(compiler: str, directory: Path) -> None:
    """Compile the production period helper plus actual aggregate backend."""
    target = directory / "room-period-chart"
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
        "-I",
        str(ROOT / "include"),
        str(ROOT / "tests/room-period-chart-native.cpp"),
        str(ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.cpp"),
        "-o",
        str(target),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(target)], check=True, cwd=ROOT)


def main() -> None:
    """Run ASAN/UBSAN against the exact production helper."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("A C++20 compiler is required")
    with TemporaryDirectory(prefix="usp-room-period-") as temporary:
        compile_and_run(compiler, Path(temporary))
    logger.info("ROOM_PERIOD_NATIVE_RUN=PASS actual_helper no_device=1")


if __name__ == "__main__":
    main()
