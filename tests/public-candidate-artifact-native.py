#!/usr/bin/env python3
"""Test public candidate recording and artifact selection using dummy bytes."""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def call(
    work: Path, command: list[str], expected: bool, name: str, env=None
) -> None:
    """Retain CLI output and require a deliberate success or refusal."""
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=20, env=env
    )
    (work / (name + ".log")).write_text(result.stdout + result.stderr)
    if (result.returncode == 0) != expected:
        raise AssertionError(f"Unexpected candidate tool result: {name}")


def prepare(work: Path):
    """Record a dummy candidate and return isolated test commands."""
    work.mkdir(parents=True, exist_ok=False)
    manifest = work / "manifest.json"
    manifest.write_text(json.dumps({"version": "0.0.0-test"}))
    image = work / "dummy.bin"
    image.write_bytes(b"synthetic test only")
    status_file = work / "release-status.json"
    record = [
        sys.executable,
        str(ROOT / "scripts/record-public-candidate.py"),
        str(image),
        str(status_file),
        "--manifest",
        str(manifest),
    ]
    env = dict(os.environ, GITHUB_RUN_ID="8123", GITHUB_SHA="0" * 40)
    call(work, record, True, "record", env)
    status = json.loads(status_file.read_text())
    assert status["public_first_flash_hardware_validated"] is False
    assert (
        status["hardware_acceptance"]["factory_sha256"]
        == hashlib.sha256(image.read_bytes()).hexdigest()
    )
    selector = [
        sys.executable,
        str(ROOT / "scripts/public-release-artifact.py"),
        "--site-dir",
        str(work),
        "--github-output",
        str(work / "github-output"),
    ]
    return status, status_file, record, selector, env


def run(work: Path) -> None:
    """Keep the saved candidate unaccepted and require valid artifact IDs."""
    status, status_file, record, selector, env = prepare(work)
    call(work, selector, False, "unaccepted")
    assert not (work / "github-output").exists()
    status["public_first_flash_hardware_validated"] = True
    status["hardware_acceptance"]["evidence"] = "synthetic fixture only"
    status_file.write_text(json.dumps(status))
    call(work, selector, True, "accepted-test")
    assert (work / "github-output").read_text() == (
        "run_id=8123\nartifact_name=public-first-flash-8123\n"
    )
    rejected_ids(work, selector, status)
    before = status_file.read_bytes()
    call(work, record, False, "no-overwrite", env)
    assert status_file.read_bytes() == before
    (work / "report.json").write_text(
        json.dumps(
            {
                "status": "PASS",
                "invalid_run_ids": 5,
                "synthetic_only": True,
                "device_contact": False,
                "builds": 0,
                "publication_attempts": 0,
            },
            indent=2,
        )
        + "\n"
    )
    logger.info(
        "PUBLIC_CANDIDATE_ARTIFACT_NATIVE=PASS no_implicit_acceptance "
        "invalid_ids5 no_output_injection no_overwrite"
    )


def rejected_ids(work: Path, selector: list[str], status: dict) -> None:
    """Disallow output-file injection and invalid GitHub workflow run IDs."""
    output = work / "github-output"
    for index, value in enumerate((0, -1, True, None, "8123\nname=wrong")):
        changed = deepcopy(status)
        changed["hardware_acceptance"]["artifact_run_id"] = value
        (work / "release-status.json").write_text(json.dumps(changed))
        before = output.read_bytes()
        call(work, selector, False, f"invalid-id-{index}")
        assert output.read_bytes() == before


def main() -> None:
    """Use temporary test files or a new project artifacts directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / "artifacts").resolve()):
            parser.error("--output must be under project artifacts")
        run(output)
    else:
        with tempfile.TemporaryDirectory(
            prefix="usp-public-artifact-"
        ) as name:
            run(Path(name) / "check")


if __name__ == "__main__":
    main()
