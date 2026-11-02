#!/usr/bin/env python3
"""Exercise actual custom-route Origin middleware with controlled HTTP metadata."""

import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def run_native():
    """Compile real project code; fake only HTTP/platform input and responses."""
    fixtures = ROOT / "tests/web_credentials_host"
    component = ROOT / "components/web_server_base"
    with tempfile.TemporaryDirectory(prefix="usp-web-origin-") as temporary:
        binary = Path(temporary) / "test"
        command = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-pthread",
            "-I",
            str(fixtures),
            "-I",
            str(component),
            str(ROOT / "tests/web_origin_host/main.cpp"),
            str(component / "web_server_base.cpp"),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


def main():
    """Verify callback rejection, compatibility and parser boundaries offline."""
    run_native()
    logger.info("WEB_CUSTOM_ORIGIN=PASS actual callbacks ASAN/UBSAN; no device writes")


if __name__ == "__main__":
    main()
