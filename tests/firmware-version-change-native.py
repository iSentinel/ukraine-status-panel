#!/usr/bin/env python3
"""Run version-dependent static checks with a simulated firmware version.

Only isolated copies of YAML profiles are changed. No candidate, image build,
device connection, or OTA is created. Use --output artifacts/<new-directory>
to retain the test inputs and individual logs.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from loguru import logger

from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]
SIMULATED_VERSION = "0.115.999-dev"


def consumers() -> list[Path]:
    """Find static checks using the common firmware-version helper."""
    return [
        path
        for path in sorted((ROOT / "tests").glob("*static.py"))
        if "from firmware_version_guard import" in path.read_text()
        and path.name != "static-test-version-safety.py"
    ]


def prepare(work: Path) -> None:
    """Copy test scripts and YAML; share only read-only reference inputs."""
    work.mkdir(parents=True, exist_ok=False)
    for name in (
        "components",
        "include",
        "assets",
        "scripts",
        "docs",
        "security",
        ".github",
    ):
        (work / name).symlink_to(ROOT / name, target_is_directory=True)
    tests = work / "tests"
    tests.mkdir()
    for source in (ROOT / "tests").iterdir():
        if source.suffix == ".py":
            shutil.copy2(source, tests / source.name)
        elif source.is_dir() and source.name != "__pycache__":
            (tests / source.name).symlink_to(source, target_is_directory=True)
    firmware = work / "firmware"
    firmware.mkdir()
    marker = f'firmware_version: "{canonical_firmware_version()}"'
    replacement = f'firmware_version: "{SIMULATED_VERSION}"'
    for source in (ROOT / "firmware").glob("ukraine-status-panel*.yaml"):
        text = source.read_text().replace(marker, replacement)
        (firmware / source.name).write_text(text)
    (firmware / "webui").symlink_to(
        ROOT / "firmware/webui", target_is_directory=True
    )


def execute(work: Path, name: str, expected: int = 0) -> dict:
    """Run a copied checker and retain its output; never run device probes."""
    command = [sys.executable, str(work / "tests" / name)]
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=60, cwd=work
    )
    (work / (name + ".log")).write_text(result.stdout + result.stderr)
    passed = (
        result.returncode == 0 if expected == 0 else result.returncode != 0
    )
    if not passed:
        raise AssertionError(f"Version isolation failed: {name}")
    return {
        "test": name,
        "exit": result.returncode,
        "expected_success": expected == 0,
    }


def negative_controls(work: Path) -> list[dict]:
    """Prove rejection of a pinned marker and a missing canonical version."""
    rows = []
    marker = f'firmware_version: "{SIMULATED_VERSION}"'
    pinned = work / "tests/synthetic-version-pin.py"
    pinned.write_text(f"MARKER = {marker!r}\n")
    rows.append(execute(work, "static-test-version-safety.py", expected=1))
    pinned.unlink()
    rows.append(execute(work, "static-test-version-safety.py"))
    yaml = work / "firmware/ukraine-status-panel.yaml"
    text = yaml.read_text()
    yaml.write_text(text.replace(marker, "removed_version: true"))
    rows.append(execute(work, "firmware_version_guard.py", expected=1))
    yaml.write_text(text)
    return rows


def run(work: Path) -> None:
    """Require every version-dependent static consumer to survive a bump."""
    selected = consumers()
    if not selected:
        raise AssertionError("No consumers found; empty coverage is invalid")
    prepare(work)
    rows = [execute(work, path.name) for path in selected]
    controls = negative_controls(work)
    report = {
        "status": "PASS",
        "canonical_version": canonical_firmware_version(),
        "simulated_version": SIMULATED_VERSION,
        "consumers": rows,
        "negative_controls": controls,
        "firmware_candidate_created": False,
        "device_contact": False,
        "firmware_builds": 0,
        "ota_attempts": 0,
    }
    (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    logger.info(
        "FIRMWARE_VERSION_CHANGE_NATIVE=PASS consumers={}", len(selected)
    )


def main() -> None:
    """Use a new artifact directory or discard ordinary temporary evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="New artifacts directory")
    args = parser.parse_args()
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / "artifacts").resolve()):
            parser.error("--output must be inside project artifacts")
        run(output)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-version-change-") as name:
            run(Path(name) / "isolated")


if __name__ == "__main__":
    main()
