#!/usr/bin/env python3
"""Verify actual Wi-Fi scan loop/lock methods without network or radio access."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    """Read an entire actual C++ method without replacing its branches."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def prepare():
    """Bind host dependencies to the real lock declaration and deferred time."""
    path = ROOT / "components/usp_wifi_scan"
    source = (path / "wifi_scan_api.cpp").read_text()
    header = (path / "wifi_scan_api.h").read_text()
    names = (
        "void WifiScanApi::loop()",
        "bool WifiScanApi::lock_(",
        "void WifiScanApi::unlock_()",
    )
    methods = "\n".join(method(source, name) for name in names)
    declaration = re.search(r"bool lock_\([^;]+;", header).group()
    defer = re.search(r"static constexpr uint32_t SCAN_DEFER_MS[^;]+;", header).group()
    content = (ROOT / "tests/wifi-scan-loop-host.cpp.in").read_text()
    content = content.replace("@LOCK_DECL@", declaration)
    content = content.replace("@DEFER_DECL@", defer)
    return content.replace("@METHODS@", methods)


def main():
    """Observe requested waits and state transitions; never start a real scan."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    with tempfile.TemporaryDirectory(prefix="usp-wifi-loop-") as directory:
        folder = Path(directory)
        source, binary = folder / "actual.cpp", folder / "actual"
        source.write_text(prepare())
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(source),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    logger.info("WIFI_SCAN_LOOP_NATIVE=PASS actual methods; radio not used")


if __name__ == "__main__":
    main()
