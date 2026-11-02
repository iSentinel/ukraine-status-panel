#!/usr/bin/env python3
"""Execute the exact production SHT4x conversion/cropping expressions."""
from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/usp_optional_sht4x/optional_sht4x.cpp"


def exact_line(source: str, name: str) -> str:
    """Extract one production float assignment, including its expression."""
    match = re.search(
        rf"^\s*const float {re.escape(name)}\s*=\s*(.+?);\s*$",
        source,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"Missing production assignment: {name}")
    return f"const float {name} = {match.group(1)};"


def main() -> None:
    """Compile the production expressions and exhaust all 16-bit RH ticks."""
    source = SOURCE.read_text()
    assert "static constexpr float RAW_MAX = 65535.0f;" in source
    lines = [
        exact_line(source, "temperature"),
        exact_line(source, "humidity_raw"),
        exact_line(source, "humidity"),
    ]
    assert lines[1] == (
        "const float humidity_raw = -6.0f + 125.0f * float(data[1]) / RAW_MAX;"
    )
    assert lines[2] == (
        "const float humidity = std::clamp(humidity_raw, 0.0f, 100.0f);"
    )
    body = "\n  ".join(lines)
    harness = f'''#include <algorithm>\n#include <cassert>\n#include <cmath>\n#include <cstdint>\n#include <iostream>\n\nstatic constexpr float RAW_MAX = 65535.0f;\n\nstruct Values {{\n  float temperature;\n  float humidity_raw;\n  float humidity;\n}};\n\nValues convert(uint16_t temperature_ticks, uint16_t humidity_ticks) {{\n  uint16_t data[2]{{temperature_ticks, humidity_ticks}};\n  {body}\n  return {{temperature, humidity_raw, humidity}};\n}}\n\nint main() {{\n  unsigned low_cropped = 0;\n  unsigned high_cropped = 0;\n  float previous = -1.0f;\n  for (uint32_t raw = 0; raw <= 65535U; ++raw) {{\n    const auto value = convert(32768U, static_cast<uint16_t>(raw));\n    const float formula = -6.0f + 125.0f * float(raw) / RAW_MAX;\n    const float expected = std::clamp(formula, 0.0f, 100.0f);\n    assert(std::fabs(value.humidity_raw - formula) < 0.0001f);\n    assert(std::fabs(value.humidity - expected) < 0.0001f);\n    assert(value.humidity >= 0.0f && value.humidity <= 100.0f);\n    assert(value.humidity >= previous);\n    previous = value.humidity;\n    low_cropped += formula < 0.0f;\n    high_cropped += formula > 100.0f;\n  }}\n  const auto zero = convert(0U, 0U);\n  const auto full = convert(65535U, 65535U);\n  const auto middle = convert(32768U, 32768U);\n  assert(zero.humidity_raw == -6.0f && zero.humidity == 0.0f);\n  assert(full.humidity_raw == 119.0f && full.humidity == 100.0f);\n  assert(middle.humidity > 56.0f && middle.humidity < 57.0f);\n  assert(low_cropped > 0U && high_cropped > 0U);\n  std::cout << "OPTIONAL_SHT4X_CONVERSION_NATIVE=PASS ticks=65536 low_crop="\n            << low_cropped << " high_crop=" << high_cropped\n            << " range=0..100 vendor_formula=1 production_expr=1\\n";\n}}\n'''
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("g++ is required")
    with tempfile.TemporaryDirectory(prefix="usp-sht4x-conversion-") as tmp:
        cpp = Path(tmp) / "test.cpp"
        binary = Path(tmp) / "test"
        cpp.write_text(harness)
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O1",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                str(cpp),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
