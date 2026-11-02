#!/usr/bin/env python3
"""Compile exact NEPTUN scanners and test fragmented, incomplete and advisory data."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Use real parser sources and optional captured upstream shapes."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threats", type=Path)
    parser.add_argument("--alerts", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if bool(args.threats) != bool(args.alerts):
        parser.error("--threats and --alerts must be supplied together")
    with TemporaryDirectory(prefix="usp-neptun-scanner-") as directory:
        binary = Path(directory) / "scanner"
        source = ROOT / "components/usp_neptun"
        command = [
            shutil.which("g++") or "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(source), str(ROOT / "tests/neptun-scanner-native.cpp"),
            str(source / "neptun_threat_parser.cpp"),
            str(source / "neptun_alert_parser.cpp"), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        command = [str(binary)]
        if args.threats:
            command += [str(args.threats), str(args.alerts)]
        result = subprocess.run(command, capture_output=True, text=True)
        text = result.stdout + result.stderr
        if args.output:
            args.output.write_text(text)
        if result.returncode:
            raise RuntimeError(text)
        logger.info("{}", text.strip())


if __name__ == "__main__":
    main()
