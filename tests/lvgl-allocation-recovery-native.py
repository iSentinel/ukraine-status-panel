#!/usr/bin/env python3
"""Execute the ESP32 production LVGL allocation functions with failed heaps.

--source may select an isolated copy for before/after proof. --output retains
the generated executable inputs under artifacts. No device is contacted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "components/lvgl/lvgl_esphome.cpp"


def method(source: str, name: str) -> str:
    """Extract the complete allocator method without rewriting its branches."""
    position = source.index(name + "(")
    start = source.rfind("\n", 0, position) + 1
    opened = source.index("{", position)
    end, depth = opened + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def production(source: str) -> str:
    """Select the ESP32 allocator without changing the separate host core."""
    declaration = re.search(
        r"^static (?:constexpr )?unsigned cap_bits\s*=.*$",
        source,
        re.MULTILINE,
    )
    if declaration is None:
        raise ValueError("ESP32 allocation capability declaration missing")
    start = declaration.start()
    body = source[start:]
    names = (
        "lv_alloc_draw_buf",
        "lv_malloc_core",
        "lv_free_core",
        "lv_realloc_core",
        "lv_mem_monitor_core",
    )
    return (
        declaration[0] + "\n" + "\n".join(method(body, name) for name in names)
    )


def compile_command(work: Path) -> list[str]:
    """Enable assertions, memory checks and strict diagnostics."""
    return [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O1",
        "-g",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        str(work / "test.cpp"),
        "-o",
        str(work / "test"),
    ]


def execute(work: Path):
    """Compile the prepared source and execute its real allocator branches."""
    command = compile_command(work)
    (work / "compile-command.json").write_text(json.dumps(command, indent=2))
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=60
    )
    (work / "compile.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError("Allocator fixture did not compile")
    execution = subprocess.run(
        [str(work / "test")], capture_output=True, text=True, timeout=20
    )
    return result, execution


def run(source: Path, work: Path) -> bool:
    """Record compile and runtime results; reject build errors."""
    work.mkdir(parents=True, exist_ok=False)
    original = source.read_bytes()
    fixture = (
        ROOT / "tests/lvgl-allocation-recovery-native.cpp.in"
    ).read_text()
    text = fixture.replace(
        "// PRODUCTION_ALLOCATOR", production(original.decode())
    )
    (work / "test.cpp").write_text(text)
    result, execution = execute(work)
    (work / "run.log").write_text(execution.stdout + execution.stderr)
    if source.read_bytes() != original:
        raise RuntimeError("Input source changed during execution")
    (work / "report.json").write_text(
        json.dumps(
            {
                "status": "PASS" if execution.returncode == 0 else "FAIL",
                "compile_exit": result.returncode,
                "run_exit": execution.returncode,
                "source": str(source),
                "source_sha256": hashlib.sha256(original).hexdigest(),
                "source_modified": False,
                "device_contact": False,
            },
            indent=2,
        )
        + "\n"
    )
    logger.info(
        "LVGL_ALLOCATION_RECOVERY_NATIVE exit={}", execution.returncode
    )
    return execution.returncode == 0


def main() -> None:
    """Support current-source regression and isolated staged comparisons."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / "artifacts").resolve()):
            parser.error("--output must be under project artifacts")
        passed = run(args.source.resolve(), output)
    else:
        with tempfile.TemporaryDirectory(
            prefix="usp-lvgl-allocation-"
        ) as name:
            passed = run(args.source.resolve(), Path(name) / "test")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
