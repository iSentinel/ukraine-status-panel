#!/usr/bin/env python3
"""Verify project-owned ESP32 NVS startup preserves stored data on failures."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/esp32/preferences.cpp"
HARNESS = ROOT / "tests/nvs_startup_host/harness.cpp.in"
FLAGS = [
    "-std=c++20",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer",
    "-fno-pie",
    "-no-pie",
    "-g",
]


def extract(source, signature):
    """Return one balanced C++ function from the canonical source."""
    start = source.index(signature)
    position = source.index("{", start)
    depth, end = 1, position + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_harness(directory):
    """Compile the exact canonical open/reset functions with host NVS stubs."""
    source = SOURCE.read_text()
    functions = extract(source, "void ESP32Preferences::open()")
    functions += "\n" + extract(source, "bool ESP32Preferences::reset()")
    code = HARNESS.read_text().replace("/* ACTUAL_FUNCTIONS */", functions)
    cpp = directory / "nvs-startup.cpp"
    binary = directory / "nvs-startup"
    cpp.write_text(code)
    subprocess.run(["g++", *FLAGS, str(cpp), "-o", str(binary)], check=True, timeout=90)
    return binary


def verify():
    """Exercise injected init/open failures, healthy startup and explicit reset."""
    with tempfile.TemporaryDirectory(prefix="usp-nvs-startup-") as name:
        binary = build_harness(Path(name))
        result = subprocess.run(
            [str(binary), "preserve"],
            check=True,
            capture_output=True,
            text=True,
            timeout=15,
        )
    output = result.stdout.strip()
    assert "NVS_STARTUP_HOST=PRESERVED" in output
    assert "error_cases=5" in output
    assert "explicit_factory_reset=PASS" in output
    logger.info("NVS_STARTUP_PRESERVE_NATIVE=PASS failures5 reset_erase1")


def main():
    """Parse standard CLI help and run the host-only regression."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    verify()


if __name__ == "__main__":
    main()
