#!/usr/bin/env python3
"""Prove shared map primitive aliases and RGB565 behavior with mutations."""
import argparse
import json
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[2] if "artifacts" in Path(__file__).parts else Path(__file__).resolve().parents[1]
FILES = (
    "usp_map_render_primitives.h",
    "usp_fullscreen_map_runs.h",
    "usp_dashboard_map_runs.h",
    "usp_map_cache_renderer.h",
    "usp_dashboard_map_renderer.h",
)

PROBE = r'''
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include "usp_map_render_primitives.h"
#include "usp_fullscreen_map_runs.h"
#include "usp_dashboard_map_runs.h"
#include "usp_map_cache_renderer.h"
#include "usp_dashboard_map_renderer.h"
using CommonRun = usp_map_render_primitives::Run;
static_assert(std::is_same_v<usp_map_cache_data::Run, CommonRun>);
static_assert(std::is_same_v<usp_dashboard_map_cache_data::Run, CommonRun>);
static_assert(std::is_same_v<decltype(CommonRun::offset), uint32_t>);
static_assert(std::is_same_v<decltype(CommonRun::length), uint16_t>);
static_assert(sizeof(CommonRun) == 8);
static_assert(alignof(CommonRun) == 4);
static_assert(offsetof(CommonRun, offset) == 0);
static_assert(offsetof(CommonRun, length) == 4);
constexpr uint16_t oracle(uint32_t rgb) {
  return static_cast<uint16_t>(
      ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
template <typename Runs>
uint64_t validate_runs(const Runs &runs, uint32_t pixels) {
  uint64_t total = 0;
  for (const auto &run : runs) {
    if (run.length == 0 || run.offset + run.length > pixels) return 0;
    total += run.length;
  }
  return total;
}
int main() {
  for (uint32_t rgb = 0; rgb <= 0xFFFFFFU; ++rgb) {
    const auto expected = oracle(rgb);
    if (usp_map_render_primitives::rgb565(rgb) != expected ||
        usp_map_cache::rgb565(rgb) != expected ||
        usp_dashboard_map_renderer::rgb565(rgb) != expected) {
      std::cerr << "RGB_MISMATCH " << rgb << '\n';
      return 1;
    }
  }
  const auto full = validate_runs(
      usp_map_cache_data::RUNS,
      uint32_t(usp_map_cache_data::WIDTH) * usp_map_cache_data::HEIGHT);
  const auto dash = validate_runs(
      usp_dashboard_map_cache_data::RUNS,
      uint32_t(usp_dashboard_map_cache_data::WIDTH) *
          usp_dashboard_map_cache_data::HEIGHT);
  if (full == 0 || dash == 0) return 2;
  std::cout << "MAP_RENDER_PRIMITIVES=PASS rgb=16777216 full_run_pixels="
            << full << " dashboard_run_pixels=" << dash << '\n';
  return 0;
}
'''


def arguments():
    """Select real production headers or an isolated integration copy."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", type=Path, default=ROOT / "include")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def structural(include_dir):
    """Require the exact bounded refactor shape before native execution."""
    common = (include_dir / FILES[0]).read_text()
    assert "uint32_t offset;" in common and "uint16_t length;" in common
    assert common.count("constexpr uint16_t rgb565(uint32_t rgb)") == 1
    for name in FILES[1:3]:
        text = (include_dir / name).read_text()
        assert 'include "usp_map_render_primitives.h"' in text
        assert "using Run = usp_map_render_primitives::Run;" in text
        assert "struct Run" not in text
    for name in FILES[3:]:
        text = (include_dir / name).read_text()
        assert text.count("return usp_map_render_primitives::rgb565(rgb);") == 1
    return True


def compile_probe(include_dir, source, binary, log):
    """Compile with warnings-as-errors so type/alias drift is a hard failure."""
    command = [
        "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(include_dir), "-I", str(ROOT / "include"),
        str(source), "-o", str(binary),
    ]
    with log.open("w") as stream:
        return subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT).returncode


def copy_headers(source, target):
    """Copy only the five bounded headers used by mutation controls."""
    target.mkdir()
    for name in FILES:
        shutil.copy2(source / name, target / name)


def positive(include_dir, output):
    """Compile and execute all RGB values and both generated run tables."""
    source = output / "probe.cpp"
    binary = output / "probe"
    source.write_text(PROBE)
    assert compile_probe(include_dir, source, binary, output / "compile.log") == 0
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (output / "run.log").write_text(run.stdout + run.stderr)
    assert run.returncode == 0, run.returncode
    assert "rgb=16777216" in run.stdout
    return run.stdout.strip()


def negative_rgb(include_dir, output):
    """A one-bit conversion mutation must compile but fail the oracle."""
    case = output / "negative-rgb"
    copy_headers(include_dir, case)
    path = case / "usp_map_render_primitives.h"
    text = path.read_text()
    changed = text.replace("((rgb >> 3) & 0x001F)", "((rgb >> 4) & 0x001F)", 1)
    assert changed != text
    path.write_text(changed)
    source = case / "probe.cpp"; source.write_text(PROBE)
    binary = case / "probe"
    assert compile_probe(case, source, binary, case / "compile.log") == 0
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    (case / "run.log").write_text(run.stdout + run.stderr)
    assert run.returncode != 0 and "RGB_MISMATCH" in run.stderr
    return run.returncode


def negative_run(include_dir, output):
    """Changing length width must be rejected by compile-time type guards."""
    case = output / "negative-run"
    copy_headers(include_dir, case)
    path = case / "usp_map_render_primitives.h"
    text = path.read_text()
    changed = text.replace("uint16_t length;", "uint32_t length;", 1)
    assert changed != text
    path.write_text(changed)
    source = case / "probe.cpp"; source.write_text(PROBE)
    rc = compile_probe(case, source, case / "probe", case / "compile.log")
    assert rc != 0
    assert "static assertion failed" in (case / "compile.log").read_text()
    return rc


def execute(include_dir, output):
    """Run positive and mutation controls and persist a machine-readable report."""
    output.mkdir(parents=True, exist_ok=False)
    structural(include_dir)
    baseline = positive(include_dir, output)
    rgb_rc = negative_rgb(include_dir, output)
    run_rc = negative_run(include_dir, output)
    report = {
        "status": "PASS",
        "rgb_values": 16777216,
        "run_aliases": 2,
        "field_type_guards": 2,
        "negative_controls": {"rgb_runtime": rgb_rc, "run_compile": run_rc},
        "baseline": baseline,
        "device_contact": False,
        "firmware_image_built": False,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    logger.info("MAP_RENDER_PRIMITIVES_SHARED=PASS rgb16777216 aliases2 negatives2")


def main():
    """Avoid core files and keep optional evidence in a caller-owned directory."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    args = arguments()
    if args.output is not None:
        execute(args.include_dir.resolve(), args.output.resolve())
        return
    with tempfile.TemporaryDirectory(prefix="usp-map-primitives-") as directory:
        execute(args.include_dir.resolve(), Path(directory) / "evidence")


if __name__ == "__main__":
    main()
