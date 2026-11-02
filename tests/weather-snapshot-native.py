#!/usr/bin/env python3
"""Run actual weather snapshot ownership and concurrency tests with ASAN/UBSAN."""

import argparse
import importlib.util
import os
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def compile_command(binary):
    """Reuse pinned dependencies and compiler settings from the weather gate."""
    source = ROOT / "tests/weather-integrity-native.py"
    spec = importlib.util.spec_from_file_location("weather_gate", source)
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    command = gate.compile_command("g++", gate.dependency_path(), binary, True)
    original = str(ROOT / "tests/weather_host/weather-integrity.cpp")
    fixture = str(ROOT / "tests/weather_host/weather-snapshot.cpp")
    return [fixture if value == original else value for value in command]


def main():
    """Compile production code with controlled platform services, then test it."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-weather-snapshot-") as directory:
        binary = Path(directory) / "snapshot"
        subprocess.run(compile_command(binary), check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        subprocess.run([str(binary)], check=True, env=environment)
    logger.info("WEATHER_SNAPSHOT_NATIVE=PASS actual code; no panel access")


if __name__ == "__main__":
    main()
