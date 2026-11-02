#!/usr/bin/env python3
"""Compile the production WX-5D detail formatter under ASAN/UBSAN."""
import json, os, shutil, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
VERSION = "7.4.3"
def dep():
    p = ROOT / "firmware/.esphome/build/ukraine-status-panel/managed_components/bblanchon__arduinojson/src"
    assert (p / "ArduinoJson.h").is_file()
    assert json.loads((p.parent / "library.json").read_text())["version"] == VERSION
    return p
def main():
    cc = shutil.which("g++"); assert cc
    with tempfile.TemporaryDirectory(prefix="usp-weather-details-") as d:
        binary = Path(d) / "test"
        cmd = [cc, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-I", str(ROOT / "tests/weather_host/stubs"),
               "-I", str(ROOT / "components/usp_weather"),
               "-I", str(ROOT / "include"), "-isystem", str(dep()),
               str(ROOT / "tests/weather-details-native.cpp"), "-o", str(binary)]
        subprocess.run(cmd, check=True); subprocess.run([str(binary)], check=True)
if __name__ == "__main__": main()
