#!/usr/bin/env python3
"""Exercise exact-image publication gates with synthetic files only."""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.0.0-test"


def fixture(work: Path) -> tuple[dict, Path]:
    """Create test-only metadata, public YAML and dummy image bytes."""
    site = work / "web-installer"
    site.mkdir(parents=True)
    (work / "firmware").mkdir()
    manifest = {
        "name": "Ukraine Status Panel",
        "version": VERSION,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": "firmware.factory.bin", "offset": 0}],
            }
        ],
    }
    (site / "manifest.json").write_text(json.dumps(manifest))
    for name in ("README.md", "index.html"):
        (site / name).write_text("SYNTHETIC TEST; no hardware acceptance\n")
    (
        work / "firmware/ukraine-status-panel-public-first-flash.yaml"
    ).write_text(f'substitutions:\n  firmware_version: "{VERSION}"\n')
    image = work / "synthetic.factory.bin"
    image.write_bytes(b"\xe9" + bytes(range(256)) * 2)
    status = {
        "schema": 1,
        "version": VERSION,
        "public_first_flash_hardware_validated": True,
        "note": "SYNTHETIC TEST ONLY",
        "hardware_acceptance": {
            "profile": "public-first-flash",
            "version": VERSION,
            "factory_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
            "factory_size": image.stat().st_size,
            "evidence": "synthetic-test-fixture",
        },
    }
    return status, image


def validator(
    work: Path,
    status: dict,
    image: Path | None,
    expected: bool,
    label: str,
    preflight=False,
) -> None:
    """Execute the real CLI and require the intended positive or refusal."""
    site = work / "web-installer"
    (site / "release-status.json").write_text(json.dumps(status))
    command = [
        sys.executable,
        str(ROOT / "scripts/validate-web-manifest.py"),
        "--site-dir",
        str(site),
        "--public-config",
        str(work / "firmware/ukraine-status-panel-public-first-flash.yaml"),
        (
            "--acceptance-preflight"
            if preflight
            else "--require-hardware-validated"
        ),
    ]
    if image is not None:
        command += ["--factory-image", str(image)]
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=20
    )
    (work / (label + ".log")).write_text(result.stdout + result.stderr)
    if (result.returncode == 0) != expected:
        raise AssertionError(
            f"{label}: unexpected gate exit {result.returncode}"
        )


def receipt_mutations(work: Path, accepted: dict, image: Path) -> int:
    """Reject malformed declarations despite a matching hardware flag."""
    mutations = [
        ("missing-receipt", None, None),
        ("wrong-profile", "profile", "production"),
        ("wrong-version", "version", "0.0.1-test"),
        ("missing-hash", "factory_sha256", None),
        ("malformed-hash", "factory_sha256", "x" * 64),
        ("boolean-size", "factory_size", True),
        ("zero-size", "factory_size", 0),
        ("missing-evidence", "evidence", ""),
    ]
    for label, key, value in mutations:
        status = deepcopy(accepted)
        if key is None:
            status.pop("hardware_acceptance")
        else:
            status["hardware_acceptance"][key] = value
        validator(work, status, image, False, label)
    return len(mutations)


def changed_images(work: Path, accepted: dict, image: Path) -> int:
    """Reject changed bytes even when file size and version match."""
    original = image.read_bytes()
    image.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
    validator(work, accepted, image, False, "changed-one-byte")
    image.write_bytes(original[:-1])
    validator(work, accepted, image, False, "truncated")
    image.write_bytes(b"")
    validator(work, accepted, image, False, "empty")
    image.write_bytes(original)
    validator(work, accepted, work / "missing.bin", False, "missing-file")
    return 4


def bundle_command(work: Path, accepted: dict, image: Path):
    """Prepare copied release scripts and explicit synthetic metadata."""
    scripts = work / "scripts"
    scripts.mkdir()
    for name in (
        "make-public-release.sh",
        "validate-web-manifest.py",
        "public_release_contract.py",
    ):
        shutil.copy2(ROOT / "scripts" / name, scripts / name)
    (work / "web-installer/release-status.json").write_text(
        json.dumps(accepted)
    )
    env = dict(os.environ)
    env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env["PATH"]
    return ["bash", str(scripts / "make-public-release.sh"), str(image)], env


def bundle(work: Path, accepted: dict, image: Path) -> None:
    """Require the archive to contain the approved copied bytes."""
    command, env = bundle_command(work, accepted, image)
    result = subprocess.run(
        command + [str(work / "approved")],
        env=env,
        capture_output=True,
        text=True,
        timeout=20,
    )
    (work / "bundle-approved.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise AssertionError("Approved synthetic archive failed")
    archive = next((work / "approved").glob("*.tar.gz"))
    with tarfile.open(archive) as stream:
        name = next(
            n for n in stream.getnames() if n.endswith("/firmware.factory.bin")
        )
        assert stream.extractfile(name).read() == image.read_bytes()
    image.write_bytes(image.read_bytes() + b"changed")
    blocked = subprocess.run(
        command + [str(work / "blocked")],
        env=env,
        capture_output=True,
        text=True,
        timeout=20,
    )
    (work / "bundle-blocked.log").write_text(blocked.stdout + blocked.stderr)
    assert blocked.returncode != 0
    assert not list((work / "blocked").glob("*.tar.gz"))


def run(work: Path) -> None:
    """Cover the CLI, declaration preflight and archive construction."""
    work.mkdir(parents=True, exist_ok=False)
    accepted, image = fixture(work)
    validator(work, accepted, image, True, "approved-image")
    validator(work, accepted, None, False, "image-required")
    validator(work, accepted, None, True, "preflight-only", preflight=True)
    unaccepted = deepcopy(accepted)
    unaccepted["public_first_flash_hardware_validated"] = False
    validator(work, unaccepted, image, False, "hardware-false")
    cases = 4 + receipt_mutations(work, accepted, image)
    cases += changed_images(work, accepted, image)
    bundle(work, accepted, image)
    (work / "report.json").write_text(
        json.dumps(
            {
                "status": "PASS",
                "cli_cases": cases,
                "bundle_cases": 2,
                "synthetic_only": True,
                "device_contact": False,
                "firmware_built": False,
                "publication_attempts": 0,
            },
            indent=2,
        )
        + "\n"
    )
    logger.info("PUBLIC_RELEASE_IMAGE_NATIVE=PASS cli={} bundle=2", cases)


def main() -> None:
    """Retain evidence only in a new project artifacts directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / "artifacts").resolve()):
            parser.error("--output must be under project artifacts")
        run(output)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-release-image-") as name:
            run(Path(name) / "check")


if __name__ == "__main__":
    main()
