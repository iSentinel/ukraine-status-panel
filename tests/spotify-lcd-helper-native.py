#!/usr/bin/env python3
"""Check actual Spotify captions and LCD helpers with a host LVGL substitute."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def pinned_json():
    """Reuse the existing checker for the exact ArduinoJson version."""
    path = ROOT / "tests/spotify-playback-presentation-native.py"
    spec = importlib.util.spec_from_file_location("spotify_presentation", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.dep()


def prepare_fixture(folder):
    """Copy production bytes unchanged; replace only the shared LVGL boundary."""
    header = ROOT / "include/usp_spotify_lcd.h"
    shutil.copyfile(header, folder / header.name)
    assert (folder / header.name).read_bytes() == header.read_bytes()
    shutil.copyfile(
        ROOT / "tests/spotify_lcd_host/usp_lcd_navigation_controller.h",
        folder / "usp_lcd_navigation_controller.h",
    )
    alias = folder / "esphome/components/usp_spotify_auth"
    alias.mkdir(parents=True)
    for name in ("playback_captions.h", "control_captions.h", "value_control_policy.h"):
        original = ROOT / "components/usp_spotify_auth" / name
        (alias / name).write_text(
            "#pragma once\n#include " + json.dumps(str(original)) + "\n"
        )


def execute(compiler, folder, source, dependency):
    """Compile one actual helper contract with ASAN/UBSAN and no provider I/O."""
    binary = folder / source.stem
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(folder),
        "-I",
        str(ROOT / "include"),
        "-I",
        str(ROOT / "components/usp_spotify_auth"),
        "-isystem",
        str(dependency),
        str(source),
        "-o",
        str(binary),
    ]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        logger.error("{}", result.stderr[-2500:])
        result.check_returncode()
    result = subprocess.run(
        [str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=30
    )
    if result.returncode:
        logger.error("{}", result.stderr[-2500:])
        result.check_returncode()
    print(result.stdout, end="")


def main():
    """Keep GUI model checks separate from physical LCD visual acceptance."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    dependency = pinned_json()
    with tempfile.TemporaryDirectory(prefix="usp-spotify-lcd-") as name:
        folder = Path(name)
        prepare_fixture(folder)
        for source in (
            "spotify-lcd-captions-native.cpp",
            "spotify-lcd-helper-native.cpp",
            "spotify-controls-lcd-native.cpp",
        ):
            execute(compiler, folder, ROOT / "tests" / source, dependency)
    logger.info(
        "SPOTIFY_LCD_HELPER_NATIVE=PASS actual_helper SDK_fixture no_device"
    )


if __name__ == "__main__":
    main()
