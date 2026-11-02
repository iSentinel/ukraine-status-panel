#!/usr/bin/env python3
"""Validate production room-clock recovery and its persistent-format boundaries."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SHIMS = ROOT / "tests/room_aggregate_history_host/shims"
COMPONENT = ROOT / "components/usp_room_aggregate_history"


def compile_test(compiler, target, source, with_component):
    """Compile real recovery code with host storage adapters and sanitizers."""
    command = [
        compiler,
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined,float-cast-overflow",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        "-g",
        "-I",
        str(SHIMS),
        "-I",
        str(COMPONENT),
        str(source),
    ]
    if with_component:
        command.append(str(COMPONENT / "usp_room_aggregate_history.cpp"))
    subprocess.run(command + ["-o", str(target)], check=True, timeout=120)


def suites():
    """Keep backend and pure-format cases independently executable."""
    return (
        ("backend", ROOT / "tests/room-clock-recovery-native.cpp", True),
        (
            "classifier",
            ROOT / "tests/room_aggregate_history_host/recovery_classifier.cpp",
            False,
        ),
        (
            "marker",
            ROOT / "tests/room_aggregate_history_host/recovery_marker.cpp",
            False,
        ),
    )


def main():
    """Run current/legacy recovery, clock boundaries and corrupt-marker cases."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("A C++20 compiler is required")
    fixtures = ROOT / "tests/fixtures"
    with TemporaryDirectory(prefix="usp-room-recovery-") as directory:
        for name, source, with_component in suites():
            target = Path(directory) / name
            compile_test(compiler, target, source, with_component)
            command = [str(target)]
            if with_component:
                command += [
                    str(fixtures / "room-aggregate-legacy-midday.hex"),
                    str(fixtures / "room-aggregate-legacy-dst.hex"),
                ]
            subprocess.run(command, check=True, timeout=15)
    logger.info(
        "ROOM_CLOCK_RECOVERY_NATIVE_RUN=PASS backend15 classifier279 marker_v1_v2_faults no_device=1"
    )


if __name__ == "__main__":
    main()
