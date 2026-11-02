#!/usr/bin/env python3
"""Check bounded artwork request decisions without network or pixel buffers."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def run_checked(command):
    """Show useful compiler or sanitizer errors before stopping."""
    result = subprocess.run(
        command, cwd=ROOT, capture_output=True, text=True, timeout=60
    )
    if result.returncode:
        logger.error("{}", result.stderr[-4000:])
        result.check_returncode()
    print(result.stdout, end="")


def main():
    """Compile the real header with address and undefined behavior checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    with tempfile.TemporaryDirectory(prefix="usp-artwork-plan-") as name:
        binary = Path(name) / "native"
        run_checked(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "components/usp_spotify_auth"),
                str(ROOT / "tests/spotify-artwork-plan-native.cpp"),
                "-o",
                str(binary),
            ]
        )
        run_checked([str(binary)])
    logger.info("SPOTIFY_ARTWORK_PLAN_NATIVE=PASS no_provider no_device")


if __name__ == "__main__":
    main()
