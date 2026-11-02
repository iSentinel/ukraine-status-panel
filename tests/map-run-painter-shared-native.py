#!/usr/bin/env python3
"""Prove shared normal-path map run painting preserves renderer behavior."""
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
    "usp_map_cache_renderer.h",
    "usp_dashboard_map_renderer.h",
)

FULL_NEW = r'''  void paint_(uint8_t label, uint16_t color) {
    usp_map_render_primitives::paint_label(
        buffer_, usp_map_cache_data::STARTS, usp_map_cache_data::RUNS,
        usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT, label, color);
  }
'''
FULL_OLD = r'''  void paint_(uint8_t label, uint16_t color) {
    for (uint32_t index = usp_map_cache_data::STARTS[label];
         index < usp_map_cache_data::STARTS[label + 1]; ++index) {
      const auto &run = usp_map_cache_data::RUNS[index];
      std::fill_n(buffer_ + run.offset, run.length, color);
    }
  }
'''
DASH_NEW = r'''  void paint_(uint8_t label, uint16_t color, DirtyArea *dirty) {
    usp_map_render_primitives::paint_label(
        buffer_, usp_dashboard_map_cache_data::STARTS,
        usp_dashboard_map_cache_data::RUNS,
        usp_dashboard_map_cache_data::WIDTH * usp_dashboard_map_cache_data::HEIGHT,
        label, color,
        [dirty](uint32_t offset, uint16_t length) {
          if (dirty != nullptr) dirty->include(offset, length);
        });
  }
'''
DASH_OLD = r'''  void paint_(uint8_t label, uint16_t color, DirtyArea *dirty) {
    for (uint32_t index = usp_dashboard_map_cache_data::STARTS[label];
         index < usp_dashboard_map_cache_data::STARTS[label + 1]; ++index) {
      const auto &run = usp_dashboard_map_cache_data::RUNS[index];
      std::fill_n(buffer_ + run.offset, run.length, color);
      if (dirty != nullptr) dirty->include(run.offset, run.length);
    }
  }
'''

PROBE = r'''
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
#include "usp_fullscreen_map_runs.h"
#include "usp_dashboard_map_runs.h"
#include "legacy_usp_map_cache_renderer.h"
#include "legacy_usp_dashboard_map_renderer.h"
#include "usp_map_cache_renderer.h"
#include "usp_dashboard_map_renderer.h"

struct Palette { uint16_t value[27]{}; };

template <typename A, typename B>
bool dirty_equal(const A &a, const B &b) {
  return a.changed == b.changed && a.full == b.full && a.x1 == b.x1 &&
         a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

Palette colors(uint16_t seed) {
  Palette result{};
  for (size_t i = 0; i < 27; ++i)
    result.value[i] = static_cast<uint16_t>(seed + i * 977U);
  return result;
}

int full_normal() {
  const size_t pixels = size_t(usp_map_cache_data::WIDTH) * usp_map_cache_data::HEIGHT;
  std::vector<uint16_t> oldbuf(pixels, 0xA55A), newbuf(pixels, 0xA55A);
  legacy_usp_map_cache::Renderer old(oldbuf.data());
  usp_map_cache::Renderer current(newbuf.data());
  auto value = colors(0x1234);
  for (int step = -2; step < 27; ++step) {
    if (step >= 0) value.value[step] ^= static_cast<uint16_t>(0x0101U + step);
    const bool a = old.update(value.value);
    const bool b = current.update(value.value);
    if (a != b || oldbuf != newbuf) return 10 + step + 2;
  }
  return 0;
}

int dashboard_normal() {
  const size_t pixels = size_t(usp_dashboard_map_cache_data::WIDTH) *
                        usp_dashboard_map_cache_data::HEIGHT;
  std::vector<uint16_t> oldbuf(pixels, 0x5AA5), newbuf(pixels, 0x5AA5);
  legacy_usp_dashboard_map_renderer::Renderer old(oldbuf.data());
  usp_dashboard_map_renderer::Renderer current(newbuf.data());
  auto value = colors(0x4321);
  for (int step = -2; step < 27; ++step) {
    if (step >= 0) value.value[step] ^= static_cast<uint16_t>(0x0201U + step);
    legacy_usp_dashboard_map_renderer::DirtyArea da{};
    usp_dashboard_map_renderer::DirtyArea db{};
    const bool a = old.update(value.value, &da);
    const bool b = current.update(value.value, &db);
    if (a != b || oldbuf != newbuf || !dirty_equal(da, db)) return 50 + step + 2;
  }
  return 0;
}

int full_cold(bool supersede) {
  const size_t pixels = size_t(usp_map_cache_data::WIDTH) * usp_map_cache_data::HEIGHT;
  std::vector<uint16_t> oldbuf(pixels, 0xCAFE), newbuf(pixels, 0xCAFE);
  legacy_usp_map_cache::Renderer old(oldbuf.data());
  usp_map_cache::Renderer current(newbuf.data());
  auto first = colors(0x1000);
  auto second = colors(0x7000);
  if (old.cold_begin(first.value) != current.cold_begin(first.value)) return 90;
  if (supersede) {
    if (old.cold_step(1234) != current.cold_step(1234) || oldbuf != newbuf) return 91;
    if (old.cold_begin(second.value) != current.cold_begin(second.value)) return 92;
  }
  const uint32_t budgets[] = {1, 17, 4096, 8192, 127, 2048};
  size_t iteration = 0;
  while (old.cold_pending() || current.cold_pending()) {
    if (iteration > 5000) return 93;
    const uint32_t budget = budgets[iteration % 6];
    const bool a = old.cold_step(budget);
    const bool b = current.cold_step(budget);
    if (a != b || old.cold_step_pixels() != current.cold_step_pixels() ||
        old.cold_pending() != current.cold_pending() || oldbuf != newbuf)
      return 94;
    ++iteration;
  }
  return old.ready() == current.ready() && oldbuf == newbuf ? 0 : 95;
}

int main() {
  if (int rc = full_normal()) return rc;
  if (int rc = dashboard_normal()) return rc;
  if (int rc = full_cold(false)) return rc;
  if (int rc = full_cold(true)) return rc;
  std::cout << "MAP_RUN_PAINTER_SHARED=PASS full29 dashboard29 cold2\n";
  return 0;
}
'''


def arguments():
    """Select production include path and optional evidence directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", type=Path, default=ROOT / "include")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def replace_once(path, old, new):
    """Replace one exact bounded block or fail before running the probe."""
    text = path.read_text()
    assert text.count(old) == 1, (path, text.count(old))
    path.write_text(text.replace(old, new, 1))


def structural(include_dir):
    """Require helper ownership without touching cold or geometry lifecycles."""
    common = (include_dir / FILES[0]).read_text()
    full = (include_dir / FILES[3]).read_text()
    dash = (include_dir / FILES[4]).read_text()
    assert "template <typename Observer>" in common
    assert "std::fill_n(buffer + span.first, run.length, color);" in common
    assert full.count("usp_map_render_primitives::paint_label(") == 1
    assert dash.count("usp_map_render_primitives::paint_label(") == 1
    assert FULL_NEW in full and DASH_NEW in dash
    assert "cold_step(uint32_t" in full


def copy_headers(source, target):
    """Copy only bounded renderer/run headers into an isolated case."""
    target.mkdir(parents=True)
    for name in FILES:
        shutil.copy2(source / name, target / name)


def add_legacy(case):
    """Reconstruct accepted pre-refactor painter loops as an independent oracle."""
    full = (case / FILES[3]).read_text()
    dash = (case / FILES[4]).read_text()
    assert full.count(FULL_NEW) == 1 and dash.count(DASH_NEW) == 1
    full = full.replace(FULL_NEW, FULL_OLD, 1)
    dash = dash.replace(DASH_NEW, DASH_OLD, 1)
    full = full.replace("namespace usp_map_cache {", "namespace legacy_usp_map_cache {")
    full = full.replace("}  // namespace usp_map_cache", "}  // namespace legacy_usp_map_cache")
    dash = dash.replace("namespace usp_dashboard_map_renderer {", "namespace legacy_usp_dashboard_map_renderer {")
    dash = dash.replace("}  // namespace usp_dashboard_map_renderer", "}  // namespace legacy_usp_dashboard_map_renderer")
    (case / "legacy_usp_map_cache_renderer.h").write_text(full)
    (case / "legacy_usp_dashboard_map_renderer.h").write_text(dash)


def compile_run(case):
    """Compile and run the old/new comparison under ASAN and UBSAN."""
    source = case / "probe.cpp"
    binary = case / "probe"
    source.write_text(PROBE)
    command = ["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-I", str(case), str(source), "-o", str(binary)]
    built = subprocess.run(command, capture_output=True, text=True)
    (case / "compile.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.returncode
    env = os.environ.copy()
    env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, env=env)
    (case / "run.log").write_text(run.stdout + run.stderr)
    return run


def positive(include_dir, output):
    """Prove byte-identical normal paths and unchanged fullscreen cold lifecycle."""
    case = output / "positive"
    copy_headers(include_dir, case)
    add_legacy(case)
    run = compile_run(case)
    assert run.returncode == 0, run.returncode
    assert "MAP_RUN_PAINTER_SHARED=PASS" in run.stdout
    return run.stdout.strip()


def negative_length(include_dir, output):
    """Shortening one shared run write must diverge from the legacy renderer."""
    case = output / "negative-length"
    copy_headers(include_dir, case)
    add_legacy(case)
    path = case / FILES[0]
    replace_once(path, "std::fill_n(buffer + span.first, run.length, color);",
                 "std::fill_n(buffer + span.first, run.length - (run.length > 1 ? 1 : 0), color);")
    run = compile_run(case)
    assert run.returncode != 0
    return run.returncode


def negative_dirty(include_dir, output):
    """Losing dashboard dirty coverage must diverge from the legacy observer."""
    case = output / "negative-dirty"
    copy_headers(include_dir, case)
    add_legacy(case)
    path = case / FILES[4]
    replace_once(path, "dirty->include(offset, length);",
                 "dirty->include(offset, static_cast<uint16_t>(length - length));")
    run = compile_run(case)
    assert run.returncode != 0
    return run.returncode


def execute(include_dir, output):
    """Run positive equivalence and two behavior-sensitive mutation controls."""
    output.mkdir(parents=True, exist_ok=False)
    structural(include_dir)
    baseline = positive(include_dir, output)
    length_rc = negative_length(include_dir, output)
    dirty_rc = negative_dirty(include_dir, output)
    report = {
        "status": "PASS", "full_updates": 29, "dashboard_updates": 29,
        "cold_lifecycles": 2, "asan_ubsan": "PASS",
        "negative_controls": {"shortened_run": length_rc, "dirty_observer": dirty_rc},
        "baseline": baseline, "device_contact": False, "firmware_image_built": False,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    logger.info("MAP_RUN_PAINTER_SHARED_NATIVE=PASS full29 dashboard29 cold2 negatives2")


def main():
    """Execute in caller evidence directory or a throw-away temporary path."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    args = arguments()
    include_dir = args.include_dir.resolve()
    if args.output:
        execute(include_dir, args.output.resolve())
        return
    with tempfile.TemporaryDirectory(prefix="usp-map-run-painter-") as directory:
        execute(include_dir, Path(directory) / "evidence")


if __name__ == "__main__":
    main()
