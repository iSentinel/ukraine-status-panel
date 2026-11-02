#!/usr/bin/env python3
"""Verify the production local Spotify wrapping-key path without a device."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def command(source, binary):
    return [
        shutil.which("g++") or "g++", "-std=c++17", "-Wall", "-Wextra",
        "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(ROOT / "components/usp_spotify_auth"),
        "-I", str(ROOT / "tests/spotify_token_host/shims"),
        "-I", str(ROOT / "include"), str(source), "-o", str(binary),
    ]


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = ROOT / "tests/spotify-local-session-key-native.cpp"
    with tempfile.TemporaryDirectory(prefix="usp-spotify-local-key-") as name:
        binary = Path(name) / "local-key"
        subprocess.run(command(source, binary), check=True, timeout=60)
        result = subprocess.run(
            [str(binary)], capture_output=True, text=True, check=True, timeout=30
        )
        print(result.stdout, end="")
    logger.info("SPOTIFY_LOCAL_SESSION_KEY_NATIVE_RUN=PASS no_device")


if __name__ == "__main__":
    main()
