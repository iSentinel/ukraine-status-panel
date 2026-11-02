#!/usr/bin/env python3
"""Prove the QA DMA timeout context lifetime using exact production code.

Only host fixtures are executed. No device QA action, firmware build, or OTA
is performed. Retain inputs under a new --output artifacts directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/lvgl/lvgl_esphome.cpp"


def method(source: str) -> str:
    """Extract the complete production QA method by balanced braces."""
    marker = "bool LvglComponent::qa_present_cached_native_frame_dma()"
    start = source.index(marker)
    opened = source.index("{", start)
    end, depth = opened + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def harness(source: str) -> str:
    """Include the real ISR callback and full method, not a control-flow model."""
    start = source.index("struct QaFrameDmaWaitContext")
    end = source.index("\n#endif", start)
    real = source[start:end] + "\n" + method(source)
    fixture = (
        ROOT / "tests/qa-dma-timeout-lifetime-native.cpp.in"
    ).read_text()
    return fixture.replace("// PRODUCTION_CONTEXT_AND_METHOD", real)


def execute(work: Path, case: str) -> subprocess.CompletedProcess:
    """Run a compiled fixture with after-return memory detection enabled."""
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_stack_use_after_return=1:detect_leaks=0"
    result = subprocess.run(
        [str(work / "test"), case],
        capture_output=True,
        text=True,
        timeout=20,
        env=env,
    )
    (work / (case + ".log")).write_text(result.stdout + result.stderr)
    return result


def run(work: Path) -> None:
    """Require safe ordinary cases and sanitizer-confirmed delayed-callback UAF."""
    work.mkdir(parents=True, exist_ok=False)
    before = SOURCE.read_bytes()
    (work / "test.cpp").write_text(harness(before.decode()))
    command = [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-g",
        "-O0",
        "-fno-inline",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        str(work / "test.cpp"),
        "-o",
        str(work / "test"),
    ]
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=60
    )
    (work / "compile.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError("Compilation failed; not a confirmed lifetime bug")
    normal, rejected = execute(work, "normal"), execute(work, "reject")
    assert normal.returncode == rejected.returncode == 0
    late = execute(work, "late")
    text = late.stdout + late.stderr
    confirmed = late.returncode != 0 and "stack-use-after-return" in text
    assert (
        confirmed
    ), "Late callback did not reproduce the precise lifetime bug"
    assert SOURCE.read_bytes() == before
    (work / "report.json").write_text(
        json.dumps(
            {
                "status": "CONFIRMED_BEFORE_FAIL",
                "compile_exit": 0,
                "normal_exit": normal.returncode,
                "submit_failure_exit": rejected.returncode,
                "late_callback_exit": late.returncode,
                "sanitizer": "stack-use-after-return",
                "source_sha256": hashlib.sha256(before).hexdigest(),
                "canonical_modified": False,
                "device_contact": False,
                "firmware_builds": 0,
                "ota_attempts": 0,
            },
            indent=2,
        )
        + "\n"
    )
    logger.info("QA_DMA_TIMEOUT_PREFLIGHT=CONFIRMED late_callback_UAF")


def main() -> None:
    """Run only an explicit isolated observation, outside the normal regression."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()):
        parser.error("--output must be under project artifacts")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(output)


if __name__ == "__main__":
    main()
