#!/usr/bin/env python3
"""Verify accepted History captions and all21 production text updates."""

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
FIXTURE = ROOT / "tests/fixtures/history-text-golden.cpp"


def arguments():
    """Offer help and an optional fresh directory for compiler evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


def check_consumers():
    """Keep existing language guards, footer updates and History consumers."""
    text = YAML.read_text()
    assert "include/usp_history_text.h" in text
    start = text.index("  - id: localize_page_history")
    end = text.index("\n  - id:", start + 5)
    block = text[start:end]
    mapping = json.loads(
        (ROOT / "tests/fixtures/history-text-consumers.json").read_text()
    )
    assert len(mapping) == 7
    for key, widget in mapping:
        expression = (
            "return usp_history_text::label(id(ui_language), "
            f"usp_history_text::Label::{key});"
        )
        pattern = (
            r"id: " + re.escape(widget)
            + r"\n\s+text: !lambda \|-\n\s+" + re.escape(expression)
        )
        assert len(re.findall(pattern, block)) == 3, (key, widget)
    assert block.count("usp_history_text::label(") == 21
    assert block.count("usp_footer_text::label(") == 15
    for language in range(3):
        assert block.count(f"return id(ui_language) == {language};") == 1


def compiler_command(source, binary):
    """Enable memory checks while compiling the production catalog."""
    return [
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-pie", "-no-pie", "-I", str(ROOT / "include"),
        str(source), "-o", str(binary),
    ]


def execute(directory):
    """Keep complete compiler/runtime evidence without overwriting results."""
    directory.mkdir(parents=True, exist_ok=False)
    source, binary = directory / "test.cpp", directory / "test"
    source.write_text(FIXTURE.read_text())
    with (directory / "compile.log").open("w") as output:
        result = subprocess.run(
            compiler_command(source, binary), stdout=output, stderr=output,
            cwd=ROOT,
        )
    if result.returncode:
        logger.error("{}", (directory / "compile.log").read_text()[-2500:])
        result.check_returncode()
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, timeout=30,
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    if result.returncode:
        logger.error("{}", (directory / "run.log").read_text()[-2500:])
        result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    """Run the real catalog and verify all production consumers."""
    args = arguments()
    check_consumers()
    with tempfile.TemporaryDirectory(prefix="usp-history-i18n-") as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
