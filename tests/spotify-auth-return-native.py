#!/usr/bin/env python3
"""Execute the actual PKCE/code inbox under ASAN/UBSAN without device or network."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def run(compiler, folder):
    """Compile only production headers and deterministic host test fixtures."""
    binary = folder / "spotify-auth-return"
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(ROOT / "include"),
        str(ROOT / "tests/spotify-auth-return-native.cpp"),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    result = subprocess.run(
        [str(binary)], check=True, capture_output=True, text=True
    )
    print(result.stdout, end="")


def main():
    """Keep test products temporary and never contact the physical panel."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    with tempfile.TemporaryDirectory(prefix="usp-auth-return-") as directory:
        run(compiler, Path(directory))
    logger.info(
        "SPOTIFY_AUTH_RETURN_NATIVE_RUN=PASS no credentials/network/device"
    )


if __name__ == "__main__":
    main()
