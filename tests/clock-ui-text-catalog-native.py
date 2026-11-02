#!/usr/bin/env python3
"""Verify Clock UI exact text catalog and bounded YAML consumers."""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/clock-ui-text-golden.cpp"


def require_consumers(text):
    """Require each Clock binding while preserving the separate quality table."""
    assert "include/usp_clock_ui_text.h" in text
    start = text.index("  - id: refresh_clock_context\n")
    end = text.index("\n  - id:", start + 10)
    section = text[start:end]
    assert section.count("usp_clock_ui_text::label(") == 6
    for key in ("CLOCK", "OUTDOOR_WEATHER", "CLEAR", "ALERT", "REGION_STATUS"):
        expected = (
            f"usp_clock_ui_text::label(lang, usp_clock_ui_text::Label::{key})"
        )
        assert section.count(expected) == 1, key
    expected = "usp_clock_ui_text::label(id(ui_language), usp_clock_ui_text::Label::MAP_UNAVAILABLE)"
    assert section.count(expected) == 1
    assert "const char *quality[3][3]" in section
    assert "const unsigned language = lang < 3 ? lang : 0;" in section
    assert "quality[language][state]" in section
    assert "Cache::instance().mirror_to(" in section
    assert section.count("id(lcd_navigation_page) != 9") == 3


def execute(directory):
    """Compile and execute the independent golden fixture with sanitizers."""
    directory.mkdir(parents=True, exist_ok=False)
    source = directory / "test.cpp"
    binary = directory / "test"
    source.write_text(FIXTURE.read_text())
    cmd = [
        "g++",
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        "-I",
        str(ROOT / "include"),
        str(source),
        "-o",
        str(binary),
    ]
    compiled = subprocess.run(
        cmd, capture_output=True, text=True, cwd=ROOT, timeout=60
    )
    (directory / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, timeout=30
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    """Check canonical bindings and optionally retain native evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    require_consumers(YAML.read_text())
    with tempfile.TemporaryDirectory(prefix="usp-clock-ui-i18n-") as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
