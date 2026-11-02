#!/usr/bin/env python3
"""Verify real weather labels against generated ESPHome glyph bounds."""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
DUMP = r"""
#include "usp_weather_codes.h"
#include <cstdio>
int main() {
  for (const auto &item : usp_weather_codes::DEFINITIONS) {
    for (unsigned language = 0; language < 3; ++language) {
      std::printf("%u\t%u\t%s\n", item.code, language, item.compact[language]);
    }
  }
}
"""


def labels(program=DUMP):
    """Read descriptions from the actual C++ table instead of copying it."""
    with tempfile.TemporaryDirectory(prefix="usp-weather-labels-") as directory:
        source = Path(directory) / "labels.cpp"
        binary = Path(directory) / "labels"
        source.write_text(program)
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "include"),
                str(source),
                "-o",
                str(binary),
            ],
            check=True,
        )
        output = subprocess.check_output([str(binary)], text=True)
    return [line.split("\t", 2) for line in output.splitlines()]


def widget(source, name):
    """Read the existing label geometry without assuming a font or width."""
    start = source.index(f"id: {name}\n")
    block = source[start:].splitlines()[1:14]
    text = "\n".join(block)
    width = int(re.search(r"\bwidth: (\d+)", text)[1])
    size = int(re.search(r"text_font: usp_i18n_(\d+)", text)[1])
    return size, width


def glyphs(source, size):
    """Extract the real advance and ink bounds used by the generated font."""
    match = re.search(r"new\(usp_i18n_" + str(size) + r"\) font::Font\((\w+),", source)
    assert match, f"Generated font usp_i18n_{size} missing"
    marker = "static const font::Glyph " + match[1] + "[]"
    line = next(line for line in source.splitlines() if marker in line)
    pattern = r"\{(\d+), \([^)]*\), (\d+), (-?\d+), (-?\d+), (\d+), (\d+)\}"
    values = {
        int(row[0]): [int(value) for value in row[1:]]
        for row in re.findall(pattern, line)
    }
    assert values, "Generated glyph table could not be read"
    return values


def text_width(text, values):
    """Include glyph bearings as well as cursor advances in the width."""
    advance = 0
    right = 0
    for char in text:
        assert ord(char) in values, f"Missing glyph {char!r}"
        glyph = values[ord(char)]
        right = max(right, advance + glyph[1] + glyph[3])
        advance += glyph[0]
    return max(right, advance)


def check_labels(main, source):
    """Require every language to fit the current and forecast labels."""
    samples = []
    entries = labels()
    for name in ("weather_condition_label", "weather_day0_condition"):
        size, limit = widget(source, name)
        values = glyphs(main, size)
        for code, language, text in entries:
            width = text_width(text, values)
            assert width <= limit, (name, code, language, text, width, limit)
            samples.append(
                {
                    "widget": name,
                    "size": size,
                    "code": int(code),
                    "language": int(language),
                    "width": width,
                    "limit": limit,
                }
            )
    return samples


def main():
    """Save source-bound width evidence without contacting hardware."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--main-cpp",
        type=Path,
        required=True,
        help="Saved generated production main.cpp",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New JSON evidence path; existing files are refused",
    )
    args = parser.parse_args()
    assert not args.output.exists(), "Evidence already exists"
    raw = args.main_cpp.read_bytes()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    samples = check_labels(raw.decode(), source)
    result = {
        "status": "PASS",
        "measurements": len(samples),
        "generated_main_sha256": hashlib.sha256(raw).hexdigest(),
        "codes_header_sha256": hashlib.sha256(
            (ROOT / "include/usp_weather_codes.h").read_bytes()
        ).hexdigest(),
        "single_line_fit": True,
        "visual_hardware_acceptance": False,
        "samples": samples,
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    logger.info("WEATHER_LABEL_WIDTH=PASS measurements={}", len(samples))


if __name__ == "__main__":
    main()
