#!/usr/bin/env python3
"""Verify optional SHT4x runtime status catalog and consumers."""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def arguments():
    """Parse optional retained build directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


def callback(text: str) -> str:
    """Return the exact optional-hardware refresh script."""
    start = text.index("  - id: refresh_optional_hardware_ui\n")
    end = text.index("\n  - id: set_alert_palette", start)
    return text[start:end]


def check_consumers():
    """Require catalog consumers and unchanged sensor formatting."""
    text = YAML.read_text()
    block = callback(text)
    assert "include/usp_optional_hardware_text.h" in text
    assert block.count("usp_optional_hardware_text::label(") == 2
    assert block.count("Status::NOT_FOUND") == 1
    assert block.count("Status::STARTING") == 1
    forbidden = ("НЕМАЄ", "НЕТ", "NOT FOUND", "ЗАПУСК", "STARTING")
    for literal in forbidden:
        assert f'"{literal}"' not in block, literal
    assert block.count("id(environment_sht4x).detected()") == 2
    assert "!std::isfinite(t) || !std::isfinite(h)" in block
    expected = 'snprintf(value, sizeof(value), "%.1f°C / %.0f%%", t, h);'
    assert expected in block
    assert block.count("text_color: 0x4ADE80") == 1
    assert block.count("text_color: 0x90A4B8") == 1


def fixture() -> str:
    """Return deterministic C++ catalog oracle."""
    return r'''#include <cassert>
#include <cstring>
#include "usp_optional_hardware_text.h"
int main() {
  using S = usp_optional_hardware_text::Status;
  struct E { unsigned l; S s; const char *v; };
  const E e[] = {
    {0,S::NOT_FOUND,"НЕМАЄ"},{0,S::STARTING,"ЗАПУСК"},
    {1,S::NOT_FOUND,"НЕТ"},{1,S::STARTING,"ЗАПУСК"},
    {2,S::NOT_FOUND,"NOT FOUND"},{2,S::STARTING,"STARTING"}
  };
  for (const auto &x : e)
    assert(!std::strcmp(usp_optional_hardware_text::label(x.l,x.s),x.v));
  assert(!std::strcmp(
      usp_optional_hardware_text::label(99,S::NOT_FOUND),"NOT FOUND"));
  assert(!std::strcmp(
      usp_optional_hardware_text::label(99,S::STARTING),"STARTING"));
  assert(!std::strcmp(
      usp_optional_hardware_text::label(0,static_cast<S>(99)),""));
  return 0;
}
'''


def compile_run(directory: Path):
    """Compile and execute catalog oracle with sanitizers."""
    directory.mkdir(parents=True, exist_ok=False)
    source = directory / "test.cpp"
    binary = directory / "test"
    source.write_text(fixture())
    command = [
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-pie", "-no-pie", "-I", str(ROOT / "include"),
        str(source), "-o", str(binary),
    ]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.log").write_text(built.stdout + built.stderr)
    built.check_returncode()
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, timeout=30
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    result.check_returncode()


def main():
    """Validate bindings and catalog behavior."""
    args = arguments()
    check_consumers()
    if args.build_dir:
        compile_run(args.build_dir)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-optional-hw-") as tmp:
            compile_run(Path(tmp) / "result")
    logger.info(
        "OPTIONAL_HARDWARE_TEXT=PASS values6 fallback2 invalid1 consumers2"
    )


if __name__ == "__main__":
    main()
