#!/usr/bin/env python3
"""Verify Room UI exact text catalog and bounded YAML consumers."""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/room-ui-text-golden.cpp"


def block(text, start_id, end_id):
    """Return one exact script block without matching longer identifiers."""
    start = text.index(f"  - id: {start_id}\n")
    end = text.index(f"\n  - id: {end_id}\n", start)
    return text[start:end]


def require_consumers(text):
    """Require Room-only text bindings while retaining data and chart updates."""
    assert "include/usp_room_ui_text.h" in text
    current = block(text, "refresh_room_current_ui", "refresh_room_graph_ui")
    graph_start = text.index("  - id: refresh_room_graph_ui\n")
    graph_end = text.index("\n  - id:", graph_start + 10)
    graph = text[graph_start:graph_end]
    assert current.count("usp_room_ui_text::label(") == 7
    assert graph.count("usp_room_ui_text::label(") == 3
    for key in (
        "ROOM",
        "INDOOR",
        "OUTDOOR",
        "WEATHER",
        "HOME",
        "SENSOR_ABSENT",
        "SENSOR_WAITING",
    ):
        assert current.count(f"usp_room_ui_text::Label::{key}") == 1, key
    for key in (
        "NO_HISTORY",
        "NOW",
        "DROPPED_PREFIX",
    ):
        assert graph.count(f"usp_room_ui_text::Label::{key}") == 1, key
    assert "id(environment_sht4x).last_read_ok()" in current
    assert "usp_room_chart::render(" in graph
    assert "std::snprintf(summary, sizeof(summary)" in graph
    assert "work->model.valid_count" in graph
    assert "work->model.gap_count" in graph
    assert "usp_room_period_text::view" in graph
    assert "static_cast<unsigned>(id(room_graph_dropped_runs))" in graph


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
    with tempfile.TemporaryDirectory(prefix="usp-room-ui-i18n-") as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
