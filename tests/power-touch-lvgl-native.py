#!/usr/bin/env python3
"""Verify YAML weather hit areas with real LVGL and the production controller."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    """Load an existing verifier without copying its platform/tool discovery."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def arguments():
    """Use a fresh evidence directory so failed runs remain reviewable."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument(
        "--lvgl-root", type=Path, default=os.environ.get("USP_LVGL_ROOT")
    )
    args = parser.parse_args()
    if args.lvgl_root is None or not (args.lvgl_root / "lvgl.h").exists():
        parser.error("Set USP_LVGL_ROOT or --lvgl-root to real LVGL sources")
    return args


def fixture(directory):
    """Bind native input rectangles to the actual canonical YAML controls."""
    layout = load("power_layout", ROOT / "tests/power-touch-layout.py")
    report = layout.evidence()
    directory.mkdir()
    lines = []
    for row in report["rows"]:
        fields = row["rect"] + row["parent"]
        lines.append("  {" + ", ".join(str(value) for value in fields) + "},")
    header = (
        "#pragma once\n#include <cstddef>\n"
        "struct Rect {int x,y,w,h,px,py,pw,ph;};\n"
        "inline constexpr Rect RECTS[] = {\n"
        + "\n".join(lines)
        + "\n};\ninline constexpr size_t RECT_COUNT = sizeof(RECTS)/sizeof(RECTS[0]);\n"
    )
    (directory / "power-touch-rects.h").write_text(header)
    (directory / "layout.json").write_text(json.dumps(report, indent=2) + "\n")


def build_and_test(args, directory):
    """Compile actual LVGL, then test edges, outside points and global swipes."""
    directory.mkdir(exist_ok=False)
    fixture_dir = directory / "fixture"
    fixture(fixture_dir)
    gate = load("lvgl_gate", ROOT / "tests/lcd-navigation-lvgl-native.py")
    args.controller_include = ROOT / "include"
    args.sanitize = True
    cmake = gate.tool_path("cmake", None)
    ninja = gate.tool_path("ninja", None)
    command = gate.configure_command(args, directory / "build", cmake, ninja)
    command.append(f"-DUSP_POWER_TOUCH_FIXTURE_DIR={fixture_dir.resolve()}")
    gate.run_command(command, directory / "configure.log")
    gate.run_command(
        [
            cmake,
            "--build",
            str(directory / "build"),
            "--target",
            "power_touch",
            "-j2",
        ],
        directory / "build.log",
    )
    gate.run_command([str(directory / "build/power_touch")], directory / "result.log")
    logger.info("{}", (directory / "result.log").read_text().strip())


def main():
    """Keep native pointer checks separate from physical finger acceptance."""
    args = arguments()
    with TemporaryDirectory(prefix="usp-power-touch-") as temporary:
        directory = args.build_dir or Path(temporary) / "result"
        build_and_test(args, directory)


if __name__ == "__main__":
    main()
