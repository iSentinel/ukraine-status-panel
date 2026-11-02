#!/usr/bin/env python3
"""Compile and verify the exact NEPTUN-primary arbitration helper."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Run source failure/recovery and semantic cases with native memory checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with TemporaryDirectory(prefix="usp-source-policy-") as directory:
        binary = Path(directory) / "policy"
        command = [
            shutil.which("g++") or "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "components/usp_neptun"),
            str(ROOT / "tests/alert-source-policy-native.cpp"),
            str(ROOT / "components/usp_neptun/neptun_alert_parser.cpp"),
            "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        output = result.stdout + result.stderr
        if args.output:
            args.output.write_text(output)
        if result.returncode:
            raise RuntimeError(output)
        logger.info("{}", output.strip())


if __name__ == "__main__":
    main()
