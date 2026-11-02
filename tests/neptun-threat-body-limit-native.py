#!/usr/bin/env python3
"""Exercise the production NEPTUN threat parser around its body-size cap."""

from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    """Compile the real parser and verify mass-attack and hard-cap cases."""
    source = ROOT / "components" / "usp_neptun"
    with TemporaryDirectory(prefix="usp-neptun-body-") as directory:
        binary = Path(directory) / "body-limit"
        command = [
            shutil.which("g++") or "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(source),
            str(ROOT / "tests" / "neptun-threat-body-limit-native.cpp"),
            str(source / "neptun_threat_parser.cpp"),
            str(source / "neptun_alert_parser.cpp"),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        result = subprocess.run(
            [str(binary)], capture_output=True, text=True, check=True
        )
        logger.info("{}", (result.stdout + result.stderr).strip())


if __name__ == "__main__":
    main()
