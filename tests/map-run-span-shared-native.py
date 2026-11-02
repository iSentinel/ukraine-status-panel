#!/usr/bin/env python3
"""Validate shared map RunSpan bounds and invalid-run rejection."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    "usp_map_render_primitives.h",
    "usp_fullscreen_map_runs.h",
    "usp_dashboard_map_runs.h",
)

PROBE = r'''
#include <cstdint>
#include <iostream>
#include "usp_map_render_primitives.h"
#include "usp_fullscreen_map_runs.h"
#include "usp_dashboard_map_runs.h"

template <typename Runs>
uint32_t validate(const Runs &runs, uint32_t count, uint32_t pixels) {
  uint32_t valid = 0;
  for (uint32_t index = 0; index < count; ++index) {
    usp_map_render_primitives::RunSpan span{};
    if (!usp_map_render_primitives::resolve_run_span(
            runs[index].offset, runs[index].length, pixels, span)) return 0;
    if (span.first != runs[index].offset) return 0;
    if (span.last != runs[index].offset + runs[index].length - 1U) return 0;
    ++valid;
  }
  return valid;
}

int invalid_cases(uint32_t pixels) {
  using usp_map_render_primitives::Run;
  using usp_map_render_primitives::RunSpan;
  using usp_map_render_primitives::resolve_run_span;
  RunSpan span{};
  if (!resolve_run_span(pixels - 1U, 1U, pixels, span)) return 41;
  if (span.first != pixels - 1U || span.last != pixels - 1U) return 42;
  if (resolve_run_span(0U, 0U, pixels, span)) return 43;
  if (resolve_run_span(pixels, 1U, pixels, span)) return 44;
  if (resolve_run_span(pixels - 1U, 2U, pixels, span)) return 45;
  if (resolve_run_span(UINT32_MAX, 2U, pixels, span)) return 46;
  uint16_t buffer[4] = {1U, 2U, 3U, 4U};
  const uint32_t starts[2] = {0U, 1U};
  const Run invalid[1] = {{3U, 2U}};
  usp_map_render_primitives::paint_label(
      buffer, starts, invalid, 4U, 0U, 0xFFFFU);
  if (buffer[0] != 1U || buffer[1] != 2U ||
      buffer[2] != 3U || buffer[3] != 4U) return 47;
  return 0;
}

int main() {
  constexpr uint32_t full_pixels =
      uint32_t(usp_map_cache_data::WIDTH) * usp_map_cache_data::HEIGHT;
  constexpr uint32_t dash_pixels =
      uint32_t(usp_dashboard_map_cache_data::WIDTH) *
      usp_dashboard_map_cache_data::HEIGHT;
  const uint32_t full_count =
      sizeof(usp_map_cache_data::RUNS) / sizeof(usp_map_cache_data::RUNS[0]);
  const uint32_t dash_count = sizeof(usp_dashboard_map_cache_data::RUNS) /
                              sizeof(usp_dashboard_map_cache_data::RUNS[0]);
  if (validate(usp_map_cache_data::RUNS, full_count, full_pixels) != full_count)
    return 31;
  if (validate(usp_dashboard_map_cache_data::RUNS, dash_count, dash_pixels) !=
      dash_count)
    return 32;
  if (int rc = invalid_cases(full_pixels)) return rc;
  std::cout << "MAP_RUN_SPAN_SHARED=PASS full=" << full_count
            << " dash=" << dash_count << " invalid=5\n";
  return 0;
}
'''


def arguments():
    """Select production includes and optional evidence output."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", type=Path, default=ROOT / "include")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def copy_headers(source: Path, target: Path) -> None:
    """Copy only bounded headers used by this test."""
    target.mkdir(parents=True)
    for name in FILES:
        shutil.copy2(source / name, target / name)


def compile_run(case: Path):
    """Compile and execute the oracle under ASAN and UBSAN."""
    source = case / "probe.cpp"
    binary = case / "probe"
    source.write_text(PROBE)
    command = [
        "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(case), str(source), "-o", str(binary),
    ]
    built = subprocess.run(command, capture_output=True, text=True)
    (case / "compile.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.returncode
    env = os.environ.copy()
    env.update(
        ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
        UBSAN_OPTIONS="halt_on_error=1",
    )
    run = subprocess.run(
        [str(binary)], capture_output=True, text=True, timeout=30, env=env
    )
    (case / "run.log").write_text(run.stdout + run.stderr)
    return run


def positive(include_dir: Path, output: Path) -> str:
    """Require all generated runs and invalid cases to satisfy the oracle."""
    case = output / "positive"
    copy_headers(include_dir, case)
    run = compile_run(case)
    assert run.returncode == 0, run.returncode
    assert "MAP_RUN_SPAN_SHARED=PASS full=7981 dash=4128 invalid=5" in run.stdout
    return run.stdout.strip()


def mutate(include_dir: Path, output: Path, name: str, old: str, new: str) -> int:
    """Require one geometry mutation to fail the same native oracle."""
    case = output / f"negative-{name}"
    copy_headers(include_dir, case)
    path = case / FILES[0]
    text = path.read_text()
    assert text.count(old) == 1, (name, text.count(old))
    path.write_text(text.replace(old, new, 1))
    run = compile_run(case)
    assert run.returncode != 0, name
    return run.returncode


def structural(include_dir: Path) -> None:
    """Require reject-only geometry and no clipping helper."""
    text = (include_dir / FILES[0]).read_text()
    assert "struct RunSpan" in text
    assert "resolve_run_span(" in text
    assert "pixels - offset" in text
    assert "std::fill_n(buffer + span.first, run.length, color);" in text
    assert "clip" not in text.lower()


def execute(include_dir: Path, output: Path) -> None:
    """Run the positive oracle and two mutation controls."""
    output.mkdir(parents=True, exist_ok=False)
    structural(include_dir)
    baseline = positive(include_dir, output)
    last_rc = mutate(
        include_dir, output, "last",
        "offset + static_cast<uint32_t>(length) - 1U",
        "offset + static_cast<uint32_t>(length)",
    )
    overflow_rc = mutate(
        include_dir, output, "overflow",
        "if (static_cast<uint32_t>(length) > pixels - offset) return false;",
        "if (false) return false;",
    )
    report = {
        "status": "PASS", "full_generated_runs": 7981,
        "dashboard_generated_runs": 4128, "invalid_cases": 5,
        "asan_ubsan": "PASS", "baseline": baseline,
        "negative_controls": {"last_pixel": last_rc, "overflow_guard": overflow_rc},
        "device_contact": False, "firmware_image_built": False,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    logger.info("MAP_RUN_SPAN_SHARED_NATIVE=PASS full7981 dash4128 invalid5 negatives2")


def main() -> None:
    """Run in a requested evidence directory or an isolated temporary one."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    args = arguments()
    include_dir = args.include_dir.resolve()
    if args.output is not None:
        execute(include_dir, args.output.resolve())
        return
    with tempfile.TemporaryDirectory(prefix="usp-map-run-span-") as directory:
        execute(include_dir, Path(directory) / "evidence")


if __name__ == "__main__":
    main()
