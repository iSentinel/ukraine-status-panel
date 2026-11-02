#!/usr/bin/env python3
"""Compile the exact bounded NEPTUN positional scanner with sanitizers."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Test synthetic contracts and optionally the saved upstream snapshot."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threats", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with TemporaryDirectory(prefix="usp-neptun-points-") as directory:
        binary = Path(directory) / "points"
        source = ROOT / "components/usp_neptun"
        command = [
            shutil.which("g++") or "g++", "-std=c++17", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(source), str(ROOT / "tests/neptun-threat-points-native.cpp"),
            str(source / "neptun_threat_parser.cpp"),
            str(source / "neptun_alert_parser.cpp"), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        command = [str(binary)] + ([str(args.threats)] if args.threats else [])
        result = subprocess.run(command, capture_output=True, text=True)
        text = result.stdout + result.stderr
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text)
        if result.returncode:
            raise RuntimeError(text)
        logger.info("{}", text.strip())


if __name__ == "__main__":
    main()
