#!/usr/bin/env python3
"""Verify Alarm Controls exact text catalog and bounded YAML consumers."""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/alarm-controls-text-golden.cpp"


def block(text, start_id, end_id):
    """Return one exact script block without matching longer identifiers."""
    start = text.index(f"  - id: {start_id}\n")
    end = text.index(f"\n  - id: {end_id}\n", start)
    return text[start:end]


def require_consumers(text):
    """Require all named bindings and the existing alarm update actions."""
    assert "include/usp_alarm_controls_text.h" in text
    labels = block(text, "refresh_alarm_labels", "refresh_alarm_state")
    state = block(text, "refresh_alarm_state", "schedule_room_current_refresh")
    assert labels.count("usp_alarm_controls_text::label(") == 9
    assert state.count("usp_alarm_controls_text::label(") == 3
    for key in (
        "CLOCK",
        "TIME",
        "REPEAT",
        "NOTE",
        "SOUND_ON",
        "SOUND_OFF",
        "ON",
        "OFF",
        "SAVE",
    ):
        assert labels.count(f"Label::{key}") == 1, key
    for key in ("NOTIFY_SOUND_ON", "NOTIFY_SOUND_OFF", "DISMISS"):
        assert state.count(f"Label::{key}") == 1, key
    assert "usp_alarm_ui::status(" in state
    assert labels.count("0x166534") >= 2 and labels.count("0x263D57") >= 2
    assert "script.execute: refresh_alarm_days" in labels
    assert "component.update: web_alarm_status" in state


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
    with tempfile.TemporaryDirectory(
        prefix="usp-alarm-controls-i18n-"
    ) as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
