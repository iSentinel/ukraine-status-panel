#!/usr/bin/env python3
"""Compile and run the physical touch event-time helper with sanitizers."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger
ROOT = Path(__file__).resolve().parents[1]

def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    return parser.parse_args()

def main(args):
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError(f"Compiler not found: {args.compiler}")
    with tempfile.TemporaryDirectory(prefix="usp-touch-time-") as directory:
        binary = Path(directory) / "touch-event-time"
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I", str(ROOT / "include"),
                   str(ROOT / "tests/touch-event-time-native.cpp"), "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    logger.info("TOUCH_EVENT_TIME_HELPER=PASS")

if __name__ == "__main__":
    main(arguments())
