#!/usr/bin/env python3
"""Verify exact Settings sound-control captions and runtime bindings."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/sound-controls-text-golden.cpp"


def require_consumers(text: str) -> None:
    """Require the bounded two-button callback and nine exact lookups."""
    assert "include/usp_sound_controls_text.h" in text
    start = text.index("  - id: refresh_sound_controls\n")
    end = text.index("\n  - id:", start + 5)
    block = text[start:end]
    assert block.count("usp_sound_controls_text::label(") == 9
    assert block.count("id(ui_language) == 0U") == 2
    assert block.count("id(ui_language) == 1U") == 2
    assert block.count("id(ui_sound_enabled)") == 4
    for key, count in (
        ("SOUND_ON", 3),
        ("SOUND_OFF", 3),
        ("BUZZER_TEST", 3),
    ):
        assert block.count(f"Label::{key}") == count, key


def compiler_command(source: Path, binary: Path) -> list[str]:
    """Compile the golden callback equivalence fixture with sanitizers."""
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


def execute(directory: Path) -> None:
    """Run the production header against the accepted callback baseline."""
    directory.mkdir(parents=True, exist_ok=False)
    source = directory / "test.cpp"
    binary = directory / "test"
    source.write_text(FIXTURE.read_text())
    compiled = subprocess.run(
        compiler_command(source, binary),
        capture_output=True,
        text=True,
        cwd=ROOT,
        timeout=60,
    )
    (directory / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=30,
     )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main() -> None:
    """Check source wiring and native exact-text equivalence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    require_consumers(YAML.read_text())
    with tempfile.TemporaryDirectory(
        prefix="usp-sound-controls-i18n-"
    ) as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
