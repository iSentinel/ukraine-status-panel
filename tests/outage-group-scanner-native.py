#!/usr/bin/env python3
"""Compile the production-shared Yasno group-object scanner on the host."""

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests" / "outage-group-scanner-native.cpp"
BINARY = ROOT / "artifacts" / "outage-group-scanner-native"


def main() -> None:
    """Build with sanitizers and execute the structural extraction fixture."""
    BINARY.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "g++", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror",
        str(SOURCE), "-o", str(BINARY),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    result = subprocess.run(
        [str(BINARY)], check=False, cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    print(result.stdout, end="")
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
