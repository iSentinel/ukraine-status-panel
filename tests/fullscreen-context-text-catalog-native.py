#!/usr/bin/env python3
"""Check fullscreen context captions and their actual production consumer."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/fullscreen-context-text-golden.cpp"


def fixture() -> str:
    """Keep the exact accepted expected texts in an ordinary C++ source file."""
    return FIXTURE.read_text()


def arguments():
    """Offer help and an optional fresh directory for compiler evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


def check_consumers():
    """Require the bounded catalog calls and existing state normalization."""
    text = YAML.read_text()
    assert "include/usp_fullscreen_context_text.h" in text
    start = text.index("  - id: refresh_lcd_fullscreen_context")
    end = text.index("\n  - id:", start + 5)
    block = text[start:end]
    assert block.count("usp_fullscreen_context_text::quality(") == 2
    assert "usp_fullscreen_context_text::clear_label(lang)" in block
    assert "usp_fullscreen_context_text::alert_label(lang)" in block
    assert "const char *quality" not in block
    assert "id(alert_live_state) == 1 ? 1 :" in block
    assert "state == 1 ? 0 : state" in block


def compiler_command(source, binary):
    """Enable memory checks while compiling the unchanged production header."""
    return [
        "g++",
        "-std=c++17",
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


def execute(directory):
    """Keep complete diagnostic files; never overwrite an earlier result."""
    directory.mkdir(parents=True, exist_ok=False)
    source, binary = directory / "test.cpp", directory / "test"
    source.write_text(fixture())
    with (directory / "compile.log").open("w") as output:
        result = subprocess.run(
            compiler_command(source, binary),
            stdout=output,
            stderr=output,
            cwd=ROOT,
        )
    if result.returncode:
        logger.error("{}", (directory / "compile.log").read_text()[-2500:])
        result.check_returncode()
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    if result.returncode:
        logger.error("{}", (directory / "run.log").read_text()[-2500:])
        result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    """Run the actual header with accepted text; no device action is performed."""
    args = arguments()
    check_consumers()
    with tempfile.TemporaryDirectory(prefix="usp-context-i18n-") as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
