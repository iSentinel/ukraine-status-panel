#!/usr/bin/env python3
"""Run canonical Spotify session-coordinator concurrency tests."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/usp_spotify_auth"


def main() -> None:
    """Compile the coordinator test and execute it with sanitizers."""
    compiler = shutil.which("g++") or "g++"
    source = ROOT / "tests/spotify-session-coordinator-native.cpp"
    with tempfile.TemporaryDirectory(prefix="usp-session-coord-") as temp:
        binary = Path(temp) / "coordinator"
        command = [
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(COMPONENT), "-I", str(ROOT / "include"),
            str(source), "-o", str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        result = subprocess.run(
            [str(binary)], cwd=ROOT, capture_output=True, text=True,
            timeout=30, check=True
        )
        print(result.stdout, end="")
    logger.info("SPOTIFY_SESSION_COORDINATOR_CANONICAL=PASS")


if __name__ == "__main__":
    main()
