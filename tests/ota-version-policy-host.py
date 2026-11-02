#!/usr/bin/env python3
"""Compile actual OtaGuard version methods and exercise version policy boundaries."""

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    """Copy one complete actual method without changing its policy."""
    start = source.index("bool OtaGuard::" + name + "(")
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def fixture(component, cases):
    """Supply only the class declaration around the actual policy methods."""
    prefix = """
#include "version_policy.h"
#include <cstdint>
#include <cctype>
#include <limits>
#include <string>
class OtaGuard {
 public:
  static bool parse_version_core_(const std::string &, uint32_t out[3]);
  bool version_allowed(const std::string &, const std::string &, bool) const;
};
"""
    source = (component / "ota_guard.cpp").read_text()
    methods = "\n".join(
        extract(source, name) for name in ("parse_version_core_", "version_allowed")
    )
    return prefix + methods + "\n" + cases.read_text()


def main():
    """Run policy checks under address and undefined-behavior sanitizers."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--component", type=Path, default=ROOT / "components/usp_ota_guard"
    )
    parser.add_argument(
        "--cases", type=Path, default=ROOT / "tests/ota_version_host/policy.cpp"
    )
    args = parser.parse_args()
    compiler = shutil.which("g++")
    assert compiler, "A host C++ compiler is required"
    with tempfile.TemporaryDirectory(prefix="usp-ota-version-") as directory:
        cpp, binary = Path(directory) / "policy.cpp", Path(directory) / "policy"
        cpp.write_text(fixture(args.component, args.cases))
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I",
                str(args.component),
                str(cpp),
                "-o",
                str(binary),
            ],
            check=True,
        )
        environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=1")
        subprocess.run([str(binary)], env=environment, check=True)
    logger.info("Actual OtaGuard version policy: PASS; ASAN/UBSAN")


if __name__ == "__main__":
    main()
