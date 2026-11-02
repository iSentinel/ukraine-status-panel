#!/usr/bin/env python3
"""Test idle Digest lifetime, bounded client slots and rejected request handling."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Run actual production state logic with address and undefined checks."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-digest-session-") as temp:
        binary = Path(temp) / "sessions"
        subprocess.run(
            [
                "g++",
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-fno-pie",
                "-no-pie",
                "-I",
                str(ROOT / "components/web_server_idf"),
                str(ROOT / "tests/digest-session-native.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
            timeout=60,
        )
        subprocess.run([str(binary)], check=True, timeout=10)
    logger.info(
        "DIGEST_SESSION_NATIVE=PASS renewal protected10 replay expiry wrap"
    )


if __name__ == "__main__":
    main()
