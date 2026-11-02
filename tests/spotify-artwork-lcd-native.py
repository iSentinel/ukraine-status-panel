#!/usr/bin/env python3
"""Check stable artwork descriptors and source replacement with actual native LVGL."""
import argparse
from pathlib import Path
import runpy
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def build(directory):
    """Use the existing tool discovery and the pinned installed LVGL sources."""
    gate = runpy.run_path(str(ROOT / "tests/lcd-navigation-lvgl-native.py"))
    decoder = runpy.run_path(
        str(ROOT / "tests/spotify-artwork-decoder-native.py")
    )
    lvgl = decoder["lvgl_path"]()
    cmake = gate["tool_path"]("cmake", None)
    ninja = gate["tool_path"]("ninja", None)
    directory.mkdir(parents=True)
    gate["run_command"](
        [
            cmake,
            "-S",
            str(ROOT / "tests/spotify_artwork_lcd_host"),
            "-B",
            str(directory / "build"),
            "-G",
            "Ninja",
            "-DCMAKE_MAKE_PROGRAM=" + ninja,
            "-DUSP_PROJECT_ROOT=" + str(ROOT),
            "-DUSP_LVGL_ROOT=" + str(lvgl),
            "-DCMAKE_BUILD_TYPE=Debug",
        ],
        directory / "configure.log",
    )
    gate["run_command"](
        [cmake, "--build", str(directory / "build"), "-j2"],
        directory / "build.log",
    )
    gate["run_command"](
        [str(directory / "build/artwork_lcd")], directory / "result.log"
    )
    logger.info("{}", (directory / "result.log").read_text().strip())


def main():
    """Save optional evidence separately from production builds and device acceptance."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-artwork-lcd-") as name:
        directory = args.build_dir or Path(name) / "result"
        if directory.exists():
            raise RuntimeError(
                "Use a new build directory to preserve evidence"
            )
        build(directory)
    logger.info(
        "SPOTIFY_ARTWORK_LCD_NATIVE=PASS actual_LVGL provider_io=0 device_io=0"
    )


if __name__ == "__main__":
    main()
