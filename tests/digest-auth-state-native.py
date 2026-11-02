#!/usr/bin/env python3
"""Compile the production Digest nonce state helper with sanitizers."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    """Run the bounded nonce/replay state helper on the host."""
    with tempfile.TemporaryDirectory(prefix="usp-digest-state-") as temporary:
        binary = Path(temporary) / "digest-auth-state"
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-g",
                f"-I{ROOT}",
                str(ROOT / "tests/digest-auth-state-native.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
