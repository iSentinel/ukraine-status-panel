#!/usr/bin/env python3
"""Compile and run the alert-region batch helper under ASAN/UBSAN."""
from pathlib import Path
import os
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    """Run the documented checks and report failures directly."""
    with tempfile.TemporaryDirectory(prefix="usp-alert-region-batch-") as td:
        output = Path(td) / "alert-region-batch"
        cmd = [
            "g++",
            "-std=c++17",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            f"-I{ROOT / 'firmware/include'}",
            str(ROOT / "tests/alert-region-batch-native.cpp"),
            "-o",
            str(output),
        ]
        subprocess.run(cmd, check=True)
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        subprocess.run([str(output)], check=True, env=env)
    logger.info("ALERT_REGION_BATCH_NATIVE_RUN=PASS")


if __name__ == "__main__":
    main()
