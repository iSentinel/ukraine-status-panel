#!/usr/bin/env python3
"""Validate the N6 aggregate cursor and legacy migration on copied sources."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures"


def compile_test(compiler: str, target: Path) -> None:
    """Compile the copied component with sanitizers."""
    command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined,float-cast-overflow",
               "-fno-omit-frame-pointer", "-g",
               "-I", str(ROOT / "tests/room_aggregate_history_host/shims"),
               "-I", str(ROOT / "components/usp_room_aggregate_history"),
               str(ROOT / "tests/room-cursor-migration-native.cpp"),
               str(ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.cpp"),
               "-o", str(target)]
    subprocess.run(command, check=True)


def main() -> None:
    """Compile and exercise current-format and exact legacy fixtures."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("A C++20 compiler is required")
    with TemporaryDirectory(prefix="usp-room-cursor-") as temporary:
        target = Path(temporary) / "room-cursor"
        compile_test(compiler, target)
        subprocess.run([str(target), str(FIXTURES / "room-aggregate-legacy-midday.hex"),
                        str(FIXTURES / "room-aggregate-legacy-dst.hex")], check=True)
    logger.info("ROOM_CURSOR_N6_NATIVE_RUN=PASS no_device=1")


if __name__ == "__main__":
    main()
