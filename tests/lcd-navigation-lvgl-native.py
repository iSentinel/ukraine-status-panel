#!/usr/bin/env python3
"""Test the LCD controller with real LVGL pointer processing on the host."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def arguments():
    """Describe the library source and optional saved build evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--lvgl-root", type=Path, default=os.environ.get("USP_LVGL_ROOT")
    )
    parser.add_argument(
        "--build-dir", type=Path, default=os.environ.get("USP_LVGL_BUILD_DIR")
    )
    parser.add_argument(
        "--controller-include", type=Path, default=ROOT / "include"
    )
    parser.add_argument("--cmake", type=Path)
    parser.add_argument("--ninja", type=Path)
    parser.add_argument(
        "--no-sanitize",
        action="store_false",
        dest="sanitize",
        help="Disable AddressSanitizer and UndefinedBehaviorSanitizer",
    )
    args = parser.parse_args()
    if args.lvgl_root is None or not (args.lvgl_root / "lvgl.h").is_file():
        parser.error("--lvgl-root must contain real LVGL sources")
    return args


def tool_path(name, explicit):
    """Use an installed tool or the existing ESPHome tool bundle."""
    if explicit:
        if explicit.is_file():
            return str(explicit.resolve())
        raise FileNotFoundError(explicit)
    installed = shutil.which(name)
    if installed:
        return installed
    suffix = "*/bin/cmake" if name == "cmake" else "*/ninja"
    directory = Path.home() / ".cache/esphome/idf/tools" / name
    candidates = sorted(directory.glob(suffix))
    if candidates:
        return str(candidates[-1])
    raise FileNotFoundError(f"{name}: use --{name} with its installed path")


def run_command(command, log):
    """Keep complete evidence and show only a short diagnostic on failure."""
    with log.open("w") as stream:
        result = subprocess.run(
            command, stdout=stream, stderr=subprocess.STDOUT, check=False
        )
    if result.returncode:
        logger.error("{}", "\n".join(log.read_text().splitlines()[-20:]))
        raise RuntimeError(f"Command failed with {result.returncode}: {log}")
    return result


def configure_command(args, directory, cmake, ninja):
    """Keep the native build options separate from running the checks."""
    source = ROOT / "tests/lvgl_navigation_host"
    return [
        cmake,
        "-S",
        str(source),
        "-B",
        str(directory),
        "-G",
        "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DUSP_PROJECT_ROOT={ROOT}",
        f"-DUSP_LVGL_ROOT={args.lvgl_root.resolve()}",
        f"-DUSP_NAVIGATION_HEADER_DIR={args.controller_include.resolve()}",
        f"-DUSP_SANITIZE={'ON' if args.sanitize else 'OFF'}",
        "-DCMAKE_BUILD_TYPE=Debug",
    ]


def build_and_test(args, directory):
    """Compile the real library and run positive and negative input cases."""
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        raise RuntimeError("Use a new build directory to preserve evidence")
    cmake = tool_path("cmake", args.cmake)
    ninja = tool_path("ninja", args.ninja)
    command = configure_command(args, directory, cmake, ninja)
    run_command(command, directory / "configure.log")
    run_command(
        [cmake, "--build", str(directory), "-j2"], directory / "build.log"
    )
    run_command([str(directory / "navigation")], directory / "result.log")
    run_command(
        [str(directory / "clock_map_mirror")], directory / "map-mirror.log"
    )
    run_command(
        [str(directory / "transition_probe")], directory / "transition.log"
    )
    logger.info("{}", (directory / "transition.log").read_text().strip())
    logger.info("{}", (directory / "result.log").read_text().strip())
    run_command(
        [str(directory / "display_boot")], directory / "display-boot.log"
    )
    logger.info("{}", (directory / "display-boot.log").read_text().strip())
    run_command([str(directory / "alarm_days")], directory / "alarm-days.log")
    logger.info("{}", (directory / "alarm-days.log").read_text().strip())
    run_command([str(directory / "theme_integrity")], directory / "theme.log")
    logger.info("{}", (directory / "theme.log").read_text().strip())
    run_command(
        [str(directory / "button_alignment")],
        directory / "button-alignment.log",
    )
    logger.info("{}", (directory / "button-alignment.log").read_text().strip())
    run_command(
        [str(directory / "touch_fault")], directory / "touch-fault.log"
    )
    logger.info("{}", (directory / "touch-fault.log").read_text().strip())
    run_command([str(directory / "wifi_icon")], directory / "wifi-icon.log")
    logger.info("{}", (directory / "wifi-icon.log").read_text().strip())
    logger.info("LVGL_POINTER_HOST=PASS physical_touch=not_tested")


def main():
    """Use a temporary directory unless saved evidence is requested."""
    args = arguments()
    with TemporaryDirectory(prefix="usp-lvgl-navigation-") as temporary:
        directory = args.build_dir or Path(temporary) / "build"
        build_and_test(args, directory)


if __name__ == "__main__":
    main()
