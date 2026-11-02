#!/usr/bin/env python3
"""Compile the WX-6 allocation-free refresh gate under ASAN/UBSAN."""
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    compiler = shutil.which("g++")
    assert compiler
    with tempfile.TemporaryDirectory(prefix="usp-weather-refresh-") as tmp:
        binary = Path(tmp) / "refresh-gate"
        subprocess.run([
            compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
            "-I", str(ROOT / "include"),
            str(ROOT / "tests/weather-refresh-gate-native.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
