#!/usr/bin/env python3
"""Compile the exact optional-SHT4x reprobe policy under ASAN/UBSAN."""
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("g++ is required")
    with tempfile.TemporaryDirectory(prefix="usp-sht4x-reprobe-") as tmp:
        binary = Path(tmp) / "reprobe"
        subprocess.run([
            compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "components/usp_optional_sht4x"),
            str(ROOT / "tests/optional-sht4x-reprobe-native.cpp"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
