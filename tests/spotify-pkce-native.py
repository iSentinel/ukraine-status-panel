#!/usr/bin/env python3
"""Compile and run the actual header PKCE model under ASAN/UBSAN."""
from pathlib import Path
import os
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    """Build the actual production helper with host-only deterministic material."""
    with tempfile.TemporaryDirectory(prefix="usp-spotify-pkce-") as tmp:
        binary = Path(tmp) / "spotify-pkce"
        command = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-g",
            "-I", str(ROOT / "include"),
            str(ROOT / "tests/spotify-pkce-native.cpp"),
            "-o", str(binary),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1"
        result = subprocess.run(
            [str(binary)], check=True, cwd=ROOT, env=env,
            text=True, capture_output=True,
        )
        print(result.stdout, end="")
    logger.info("SPOTIFY_PKCE_NATIVE_RUN=PASS no network/device/credentials")


if __name__ == "__main__":
    main()
