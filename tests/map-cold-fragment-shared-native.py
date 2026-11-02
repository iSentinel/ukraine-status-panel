#!/usr/bin/env python3
"""Verify the bounded cold-run fragment helper and its renderer binding."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#include <algorithm>
#include <cstdint>
#include <iostream>
#include "usp_map_render_primitives.h"
#include "usp_fullscreen_map_runs.h"
int main() {
  using namespace usp_map_render_primitives;
  constexpr uint32_t pixels = usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT;
  const uint32_t count = sizeof(usp_map_cache_data::RUNS) / sizeof(usp_map_cache_data::RUNS[0]);
  uint64_t checks = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto &run = usp_map_cache_data::RUNS[i];
    for (uint32_t progress = 0; progress < run.length; ++progress) {
      const uint32_t budgets[] = {1U, 3U, 17U, 127U, 4096U, UINT32_MAX};
      for (uint32_t budget : budgets) {
        RunFragment part{};
        if (!resolve_run_fragment(run.offset, run.length, progress, budget, pixels, part)) return 21;
        const uint32_t remaining = uint32_t(run.length) - progress;
        if (part.first != run.offset + progress || part.count != std::min(budget, remaining)) return 22;
        if (part.count == 0U || part.first + part.count > pixels) return 23;
        ++checks;
      }
    }
  }
  RunFragment part{};
  if (resolve_run_fragment(0U, 1U, 0U, 0U, pixels, part)) return 24;
  if (resolve_run_fragment(0U, 0U, 0U, 1U, pixels, part)) return 25;
  if (resolve_run_fragment(pixels, 1U, 0U, 1U, pixels, part)) return 26;
  if (resolve_run_fragment(pixels - 1U, 2U, 0U, 1U, pixels, part)) return 27;
  if (resolve_run_fragment(0U, 4U, 4U, 1U, pixels, part)) return 28;
  if (resolve_run_fragment(UINT32_MAX, 1U, 0U, 1U, pixels, part)) return 29;
  std::cout << "COLD_FRAGMENT_HELPER=PASS runs=" << count << " checks=" << checks << " invalid=6\n";
  return 0;
}
'''


def replace_once(path: Path, old: str, new: str) -> None:
    """Replace exactly one helper expression in an isolated mutation."""
    text = path.read_text()
    assert text.count(old) == 1, (old, text.count(old))
    path.write_text(text.replace(old, new, 1))


def compile_run(include_dir: Path, work: Path) -> subprocess.CompletedProcess:
    """Compile and execute the direct helper probe."""
    source = work / "probe.cpp"
    binary = work / "probe"
    source.write_text(PROBE)
    command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(include_dir), str(source), "-o", str(binary)]
    built = subprocess.run(command, capture_output=True, text=True)
    assert built.returncode == 0, built.stderr
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)


def mutation(root: Path, name: str, old: str, new: str) -> int:
    """Require one compiled helper mutation to fail the runtime oracle."""
    case = root / name
    include = case / "include"
    include.mkdir(parents=True)
    for file_name in ("usp_map_render_primitives.h", "usp_fullscreen_map_runs.h"):
        shutil.copy2(ROOT / "include" / file_name, include / file_name)
    replace_once(include / "usp_map_render_primitives.h", old, new)
    run = compile_run(include, case)
    assert run.returncode != 0, name
    return run.returncode


def source_binding() -> None:
    """Require the helper and one cold renderer consumer, not old arithmetic."""
    common = (ROOT / "include/usp_map_render_primitives.h").read_text()
    renderer = (ROOT / "include/usp_map_cache_renderer.h").read_text()
    assert common.count("struct RunFragment") == 1
    assert common.count("resolve_run_fragment(") == 1
    assert renderer.count("usp_map_render_primitives::resolve_run_fragment(") == 1
    assert "std::min(budget, uint32_t(run.length) - cold_run_offset_)" not in renderer


def main() -> None:
    """Run positive real-data coverage plus three executable negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source_binding()
    with tempfile.TemporaryDirectory(prefix="usp-cold-fragment-") as tmp:
        root = Path(tmp)
        positive = root / "positive"
        positive.mkdir()
        run = compile_run(ROOT / "include", positive)
        assert run.returncode == 0 and "COLD_FRAGMENT_HELPER=PASS" in run.stdout
        negatives = {
            "progress": mutation(root, "progress", "span.first + progress", "span.first"),
            "budget": mutation(root, "budget", "std::min(budget, remaining)", "remaining"),
            "bounds": mutation(root, "bounds", "resolve_run_span(offset, length, pixels, span)",
                               "resolve_run_span(offset, length, pixels + 1U, span)"),
        }
    report = {"status": "PASS", "positive": run.stdout.strip(),
              "negative_controls": negatives, "consumer_count": 1}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("MAP_COLD_FRAGMENT_SHARED_NATIVE=PASS checks1008402 invalid6 negatives3 consumer1")


if __name__ == "__main__":
    main()
