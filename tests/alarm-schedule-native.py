#!/usr/bin/env python3
"""Check scheduling and persistence without accessing the panel."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def compile_case(compiler, source, output, manager=False):
    """Compile production logic with strict warnings and memory checks."""
    command = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-g", "-I", str(ROOT / "include"),
        "-I", str(ROOT / "components/usp_alarm"),
        "-I", str(ROOT / "tests/alarm_host/shims"),
        str(ROOT / "tests" / source),
    ]
    if manager:
        command.append(str(ROOT / "components/usp_alarm/alarm_manager.cpp"))
    subprocess.run(command + ["-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)


def main():
    """Keep the physical device, real clock, NVS and buzzer untouched."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("A C++17 compiler is required")
    with TemporaryDirectory(prefix="usp-alarms-") as temporary:
        target = Path(temporary)
        compile_case(
            compiler, "alarm-schedule-native.cpp", target / "schedule"
        )
        compile_case(
            compiler, "alarm-manager-native.cpp", target / "manager", True
        )
    logger.info("ALARM_NATIVE=PASS real_device_time_changed=0 audible_test=0")


if __name__ == "__main__":
    main()
