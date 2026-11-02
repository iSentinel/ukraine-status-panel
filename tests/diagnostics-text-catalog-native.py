#!/usr/bin/env python3
"""Verify Diagnostics text catalog, 30 page updates and 6 touch-event updates."""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/diagnostics-text-golden.cpp"
CONSUMERS = ROOT / "tests/fixtures/diagnostics-text-consumers.json"


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


def call(key: str) -> str:
    return ("return usp_diagnostics_text::label(id(ui_language), "
            f"usp_diagnostics_text::Label::{key});")


def update_pattern(widget: str, expression: str) -> str:
    return (r"id: " + re.escape(widget) + r"\n\s+text: !lambda \|-\n\s+" + re.escape(expression))


def check_consumers():
    text = YAML.read_text()
    assert "include/usp_diagnostics_text.h" in text
    mapping = json.loads(CONSUMERS.read_text())
    start = text.index("  - id: localize_page_diagnostics")
    end = text.index("\n  - id:", start + 5)
    block = text[start:end]
    assert len(mapping["localize"]) == 10
    for key, widget in mapping["localize"]:
        assert len(re.findall(update_pattern(widget, call(key)), block)) == 3, (key, widget)
    assert block.count("usp_diagnostics_text::label(") == 30
    for language in range(3):
        assert block.count(f"return id(ui_language) == {language};") == 1
    for tail in ("refresh_runtime_i18n", "refresh_stack_i18n", "refresh_optional_hardware_ui"):
        assert f"script.execute: {tail}" in block

    widget_start = text.index("              id: lv_touch_test")
    widget_end = text.index("          - label:\n              id: backlight_value_label", widget_start)
    widget = text[widget_start:widget_end]
    assert widget.count("usp_diagnostics_text::label(") == 6
    assert len(re.findall(update_pattern("lv_touch_test", call("TOUCH_OK")), widget)) == 3
    assert len(re.findall(update_pattern("lv_touch_test", call("TOUCH_TEST")), widget)) == 3
    assert widget.count("bg_color: 0x168A50") == 1
    assert widget.count("bg_color: 0x16263A") >= 1
    assert widget.count("text: \"TOUCH TEST\"") == 1  # initial EN widget text remains literal


def compiler_command(source: Path, binary: Path):
    return ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-I", str(ROOT / "include"), str(source), "-o", str(binary)]


def execute(directory: Path):
    directory.mkdir(parents=True, exist_ok=False)
    source, binary = directory / "test.cpp", directory / "test"
    source.write_text(FIXTURE.read_text())
    compile_result = subprocess.run(compiler_command(source, binary), capture_output=True, text=True, cwd=ROOT)
    (directory / "compile.log").write_text(compile_result.stdout + compile_result.stderr)
    if compile_result.returncode:
        raise subprocess.CalledProcessError(compile_result.returncode, compiler_command(source, binary))
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    args = arguments()
    check_consumers()
    with tempfile.TemporaryDirectory(prefix="usp-diagnostics-i18n-") as tmp:
        execute(args.build_dir or Path(tmp) / "result")

if __name__ == "__main__":
    main()
