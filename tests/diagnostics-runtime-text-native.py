#!/usr/bin/env python3
"""Verify six Diagnostics runtime text bindings and catalog values."""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIELDS = ("STACK", "UPTIME", "VOLTAGE", "HEAP", "PSRAM", "BACKLIGHT")


def arguments():
    """Parse an optional retained build directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


def callback(text: str, name: str) -> str:
    """Return one exact script block from the firmware source."""
    start = text.index("  - id: " + name + "\n")
    next_script = text.find("\n  - id:", start + 10)
    next_section = text.find("\ntime:\n", start + 10)
    ends = [pos for pos in (next_script, next_section) if pos >= 0]
    assert ends, name
    return text[start:min(ends)]


def check_consumers():
    """Require only the six intended runtime consumers."""
    text = YAML.read_text()
    assert "include/usp_diagnostics_runtime_text.h" in text
    stack = callback(text, "refresh_stack_i18n")
    runtime = callback(text, "refresh_runtime_i18n")
    assert stack.count("usp_diagnostics_runtime_text::parts(") == 1
    assert runtime.count("usp_diagnostics_runtime_text::parts(") == 5
    assert "Field::STACK" in stack
    for field in FIELDS[1:]:
        assert runtime.count("Field::" + field) == 1, field
    forbidden = (
        "Stack: ", "Стек: ", "Uptime: ", "Час роботи: ",
        "Время работы: ", "Voltage: ", "Напруга: ",
        "Напряжение: ", "Heap: ", "PSRAM: ",
        "Підсвітка: ", "Подсветка: ", "Backlight: ",
    )
    for literal in forbidden:
        assert literal not in stack + runtime, literal
    assert stack.count("2048.0f") == 1
    assert stack.count("1024.0f") == 1
    assert runtime.count("delay: 25ms") == 1


def fixture():
    """Return the deterministic C++ table oracle."""
    return r"""#include <cassert>
#include <cstring>
#include "usp_diagnostics_runtime_text.h"
int main() {
  using F = usp_diagnostics_runtime_text::Field;
  struct E { unsigned l; F f; const char *p; const char *u; };
  const E e[] = {
    {0,F::STACK,"Стек: "," Б"},{1,F::STACK,"Стек: "," Б"},{2,F::STACK,"Stack: "," B"},
    {0,F::UPTIME,"Час роботи: "," с"},{1,F::UPTIME,"Время работы: "," с"},{2,F::UPTIME,"Uptime: "," s"},
    {0,F::VOLTAGE,"Напруга: "," В"},{1,F::VOLTAGE,"Напряжение: "," В"},{2,F::VOLTAGE,"Voltage: "," V"},
    {0,F::HEAP,"Heap: "," кБ"},{1,F::HEAP,"Heap: "," кБ"},{2,F::HEAP,"Heap: "," kB"},
    {0,F::PSRAM,"PSRAM: "," кБ"},{1,F::PSRAM,"PSRAM: "," кБ"},{2,F::PSRAM,"PSRAM: "," kB"},
    {0,F::BACKLIGHT,"Підсвітка: ","%"},{1,F::BACKLIGHT,"Подсветка: ","%"},{2,F::BACKLIGHT,"Backlight: ","%"}
  };
  for (const auto &x : e) {
    const auto a = usp_diagnostics_runtime_text::parts(x.l, x.f);
    assert(!std::strcmp(a.prefix, x.p));
    assert(!std::strcmp(a.unit, x.u));
  }
  const auto fallback = usp_diagnostics_runtime_text::parts(99, F::UPTIME);
  assert(!std::strcmp(fallback.prefix, "Uptime: "));
  const auto invalid = usp_diagnostics_runtime_text::parts(0, static_cast<F>(99));
  assert(!std::strcmp(invalid.prefix, ""));
  assert(!std::strcmp(invalid.unit, ""));
  return 0;
}
"""


def compile_run(directory: Path):
    """Compile and execute the catalog oracle with sanitizers."""
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
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    result.check_returncode()


def main():
    """Validate production bindings and native catalog behavior."""
    args = arguments()
    check_consumers()
    if args.build_dir:
        compile_run(args.build_dir)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-diag-runtime-") as tmp:
            compile_run(Path(tmp) / "result")
    logger.info(
        "DIAGNOSTICS_RUNTIME_TEXT=PASS values18 fallback1 invalid1 consumers6"
    )


if __name__ == "__main__":
    main()
