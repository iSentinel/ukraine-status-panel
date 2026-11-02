#!/usr/bin/env python3
"""Test the real weather component with real ArduinoJson and no device access."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
ARDUINOJSON_VERSION = "7.4.3"


def dependency_path():
    """Use the production dependency or an explicitly supplied CI checkout."""
    default = ROOT / "firmware/.esphome/build/ukraine-status-panel"
    default /= "managed_components/bblanchon__arduinojson/src"
    path = Path(os.environ.get("USP_ARDUINOJSON_INCLUDE", default))
    manifest = path.parent / "library.json"
    if not (path / "ArduinoJson.h").is_file() or not manifest.is_file():
        raise RuntimeError(
            "Set USP_ARDUINOJSON_INCLUDE to the pinned ArduinoJson src folder"
        )
    if json.loads(manifest.read_text())["version"] != ARDUINOJSON_VERSION:
        raise RuntimeError("Native test and production ArduinoJson differ")
    return path


def compile_command(compiler, dependency, binary, sanitize):
    """Compile production code; only ESPHome/HTTP/task services use stubs."""
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O0",
        "-g",
        "-I",
        str(ROOT / "tests/weather_host/stubs"),
        "-I",
        str(ROOT / "components/usp_weather"),
        "-I",
        str(ROOT / "include"),
        "-isystem",
        str(dependency),
        str(ROOT / "components/usp_weather/weather_client.cpp"),
        str(ROOT / "tests/weather_host/weather-integrity.cpp"),
        "-pthread",
        "-o",
        str(binary),
    ]
    if sanitize:
        command.extend(
            [
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
            ]
        )
    return command


def main():
    """Check parser, ownership, saved strings and clock rollover boundaries."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("Install g++ for the native weather gate")
    with tempfile.TemporaryDirectory(prefix="usp-weather-") as directory:
        binary = Path(directory) / "weather-test"
        subprocess.run(
            compile_command(
                compiler, dependency_path(), binary, not args.no_sanitize
            ),
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    logger.info("Weather integrity native checks passed; hardware not used")


if __name__ == "__main__":
    main()
