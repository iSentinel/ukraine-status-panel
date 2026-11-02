#!/usr/bin/env python3
# Verify exact Restart/Confirm captions and bounded runtime binding.
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/restart-confirm-text-golden.cpp"

def require_consumers(text: str) -> None:
    assert "include/usp_restart_confirm_text.h" in text
    start = text.index("  - id: refresh_panel_restart_button\n")
    end = text.index("\n  - id: confirm_panel_restart\n", start)
    block = text[start:end]
    assert block.count("usp_restart_confirm_text::label(") == 1
    assert block.count("Label::CONFIRM") == 1
    assert block.count("Label::RESTART") == 1
    assert "id(ui_language) < 3 ? id(ui_language) : 0" in block
    assert "pending ? 0xB91C1C : 0x7F1D1D" in block
    assert "normal[]" not in block and "confirm[]" not in block

def execute(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=False)
    source = directory / "test.cpp"
    binary = directory / "test"
    source.write_text(FIXTURE.read_text())
    cmd = [
        "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-pie", "-no-pie", "-I", str(ROOT / "include"),
        str(source), "-o", str(binary),
    ]
    compiled = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, timeout=60)
    (directory / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    result.check_returncode()
    logger.info("{}", result.stdout.strip())

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    require_consumers(YAML.read_text())
    with tempfile.TemporaryDirectory(prefix="usp-restart-confirm-i18n-") as temporary:
        execute(args.build_dir or Path(temporary) / "result")

if __name__ == "__main__":
    main()
