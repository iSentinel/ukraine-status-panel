#!/usr/bin/env python3
"""Exercise flash-stack guards and the real Spotify NVS coordinator without a device."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def command(source, binary):
    """Use production headers and explicit SDK substitutes with memory checks."""
    return [
        shutil.which("g++") or "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(ROOT / "components/usp_spotify_auth"),
        "-I",
        str(ROOT / "tests/spotify_token_host/shims"),
        "-I",
        str(ROOT / "include"),
        str(source),
        "-o",
        str(binary),
    ]


def main():
    """Keep these tests separate from real NVS, eFuse and account verification."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-flash-stack-") as name:
        for filename in (
            "spotify-flash-stack-native.cpp",
            "spotify-nvs-coordinator-native.cpp",
        ):
            source = ROOT / "tests" / filename
            binary = Path(name) / source.stem
            subprocess.run(command(source, binary), check=True, timeout=60)
            result = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                check=True,
                timeout=30,
            )
            print(result.stdout, end="")
    logger.info("SPOTIFY_FLASH_STACK_NATIVE_RUN=PASS ASAN/UBSAN no_device")


if __name__ == "__main__":
    main()
