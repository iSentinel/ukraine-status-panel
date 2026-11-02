#!/usr/bin/env python3
"""Exercise accepted-checkpoint recovery using disposable release evidence."""

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    """Import the actual writer and existing bounded fixture builder."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


WRITER = load("checkpoint_writer", ROOT / "scripts/accept-firmware-checkpoint.py")
FIXTURE = load("progress_fixture", ROOT / "tests/progress-state-native.py")


def update(path, **values):
    """Change disposable JSON only, keeping negative cases easy to read."""
    data = json.loads(path.read_text())
    data.update(values)
    FIXTURE.write_json(path, data)


def proof_files(root, release):
    """Use controlled service results; no data is sent to an actual device."""
    for name in (
        "full-gates.exit",
        "production-compile.exit",
        "freeze.exit",
        "ota1.exit",
        "hardware/hardware1.exit",
    ):
        path = release / name
        path.parent.mkdir(exist_ok=True)
        path.write_text("0\n")
    (release / "hardware/hardware-uart-460800.status").write_text("PASS baud=460800\n")
    for name in (
        "build-before.txt",
        "build-after.txt",
        "hardware/final-fingerprint.txt",
    ):
        (release / name).write_text("a" * 64 + "\n")
    FIXTURE.write_json(
        release / "final-device.json", dict(version="0.115.293-dev", services=0)
    )
    (root / "scripts").mkdir()
    shutil.copy2(ROOT / "scripts/audit-progress-state.py", root / "scripts")


def fixture(root):
    """Prepare a stale checkpoint while a separate newer source is in progress."""
    state, frozen = FIXTURE.fixture(root)
    release = frozen.parent
    proof_files(root, release)
    manifest = release / "frozen-manifest.json"
    jobs = {
        kind + "_job": kind + "001"
        for kind in ("gate", "build", "ota", "uart", "hardware")
    }
    update(manifest, build_attempts=1, ota_attempts=1, **jobs)
    for key, value in jobs.items():
        (release / (key.replace("_", "-") + ".txt")).write_text(value + "\n")
    state.update(
        installed_version="0.115.286-dev",
        installed_device_version="0.115.286-dev",
        accepted_version="0.115.286-dev",
        accepted_at="2025-01-01T00:00:00+00:00",
        accepted_source_fingerprint="f" * 64,
        accepted_ota_sha256="e" * 64,
        hardware_qualification="STALE",
        latest_hardware_checkpoint={"version": "0.115.286-dev"},
        accepted_candidates=[],
        user_history={"keep": [1, 2, 3]},
        current_operation=dict(
            name="software",
            artifact_dir="artifacts/software",
            status="IN_PROGRESS",
            build_attempts=0,
            ota_attempts=0,
        ),
        active_jobs=["UNRELATED"],
    )
    FIXTURE.write_json(root / ".work-state/firmware-progress.json", state)
    return release


def preview_apply_idempotence(root, release):
    """A preview cannot write; one repair preserves future work and unknown fields."""
    path = root / ".work-state/firmware-progress.json"
    before = path.read_bytes()
    assert WRITER.run(root, release)["status"] == "PREVIEW"
    assert path.read_bytes() == before
    backup = root / "artifacts/backup.json"
    assert WRITER.run(root, release, True, backup)["status"] == "APPLIED"
    state = json.loads(path.read_text())
    assert state["installed_version"] == "0.115.293-dev"
    assert state["installed_device_version"] == "0.115.293-dev"
    assert state["accepted_version"] == "0.115.293-dev"
    assert state["accepted_source_fingerprint"] == "a" * 64
    assert state["accepted_ota_sha256"] == state["frozen_image_sha256"]
    assert state["accepted_at"] == "2026-01-01T00:00:00+00:00"
    assert state["hardware_qualification"] == "PASS_TEST"
    assert state["latest_hardware_checkpoint"]["version"] == "0.115.293-dev"
    assert any(
        item.get("version") == "0.115.293-dev"
        for item in state["accepted_candidates"]
    )
    assert state["source_version"] == "0.115.294-dev"
    assert state["source_fingerprint"] == "b" * 64
    assert state["active_jobs"] == ["UNRELATED"]
    assert state["current_operation"]["build_attempts"] == 0
    assert state["user_history"] == {"keep": [1, 2, 3]}
    assert backup.read_bytes() == before
    after = path.read_bytes()
    assert WRITER.run(root, release, True, backup)["status"] == "NO_CHANGE"
    assert path.read_bytes() == after
    assert WRITER.audit_module(root).audit(root)["status"] == "PASS"
    return 3


def current_release(root, release):
    """Only the matching release operation receives its completed counters/jobs."""
    firmware = root / "firmware/ukraine-status-panel.yaml"
    firmware.write_text(firmware.read_text().replace("0.115.294-dev", "0.115.293-dev"))
    path = root / ".work-state/firmware-progress.json"
    update(
        path,
        current_operation=dict(artifact_dir=str(release.relative_to(root))),
        active_jobs=["build001", "UNRELATED"],
    )
    WRITER.run(root, release, True, root / "artifacts/completed-backup.json")
    state = json.loads(path.read_text())
    assert state["current_operation"]["build_attempts"] == 1
    assert state["current_operation"]["ota_attempts"] == 1
    assert state["current_operation"]["hardware_job"] == "hardware001"
    assert state["active_jobs"] == ["UNRELATED"]
    assert (
        WRITER.audit_module(root).audit(root, operation_dir=release)["status"] == "PASS"
    )
    return 1


def mutations():
    """List independent refusal conditions around an otherwise valid release."""
    return [
        lambda r, d: (d / "hardware/hardware1.exit").write_text("1\n"),
        lambda r, d: (d / "hardware/hardware-uart-460800.status").write_text(
            "READY 460800\n"
        ),
        lambda r, d: (d / "frozen/firmware.ota.bin").write_text("corrupt"),
        lambda r, d: update(d / "frozen-manifest.json", build_attempts=0),
        lambda r, d: update(d / "frozen-manifest.json", ota_attempts=0),
        lambda r, d: update(
            d / "frozen-manifest.json", validation="frozen_not_uploaded"
        ),
        lambda r, d: update(d / "final-device.json", version="0.115.294-dev"),
        lambda r, d: update(d / "final-device.json", services=1),
        lambda r, d: update(d / "final-device.json", services=False),
        lambda r, d: (d / "hardware/final-fingerprint.txt").write_text("wrong"),
        lambda r, d: update(
            r / ".work-state/firmware-progress.json",
            device_version_last_verified="0.115.294-dev",
        ),
        lambda r, d: update(
            r / ".work-state/firmware-progress.json", installed_version="0.115.294-dev"
        ),
        lambda r, d: update(
            r / ".work-state/firmware-progress.json",
            current_operation=dict(status="DISPATCHED", ota_attempts=1),
        ),
    ]


def refusals():
    """Unsafe evidence must leave the original checkpoint and backup untouched."""
    for mutate in mutations():
        with TemporaryDirectory(prefix="usp-writer-refuse-") as directory:
            root = Path(directory)
            release = fixture(root)
            mutate(root, release)
            path = root / ".work-state/firmware-progress.json"
            before = path.read_bytes()
            backup = root / "artifacts/refused-backup.json"
            try:
                WRITER.run(root, release, True, backup)
            except (ValueError, OSError, KeyError, TypeError):
                pass
            else:
                raise AssertionError("Unsafe evidence was accepted")
            assert path.read_bytes() == before
            assert not backup.exists()
    return len(mutations())


def concurrent_refusal(root):
    """A changed checkpoint cannot be replaced from an earlier in-memory copy."""
    path = root / ".work-state/firmware-progress.json"
    before = path.read_text()
    path.write_text(before + "\n")
    changed = path.read_bytes()
    try:
        WRITER.persist(path, before, {}, root / "artifacts/race.json", root)
    except ValueError:
        pass
    else:
        raise AssertionError("Concurrent edit was overwritten")
    assert path.read_bytes() == changed


def backup_refusals():
    """Neither an existing backup nor a path outside artifacts may be replaced."""
    for existing in (False, True):
        with TemporaryDirectory(prefix="usp-writer-backup-") as directory:
            root = Path(directory)
            release = fixture(root)
            path = root / ".work-state/firmware-progress.json"
            before = path.read_bytes()
            backup = root / ("artifacts/exists.json" if existing else "outside.json")
            if existing:
                backup.write_text("keep me")
            try:
                WRITER.run(root, release, True, backup)
            except (ValueError, OSError):
                pass
            else:
                raise AssertionError("Unsafe backup was accepted")
            assert path.read_bytes() == before
            if existing:
                assert backup.read_text() == "keep me"
            else:
                assert not backup.exists()
    return 2


def same_version_software(root, release):
    """Closing an image must not complete independent work at the same version."""
    path = root / ".work-state/firmware-progress.json"
    update(
        path,
        current_operation=dict(
            name="software",
            artifact_dir="artifacts/software",
            status="IN_PROGRESS",
            build_attempts=0,
            ota_attempts=0,
        ),
        active_jobs=["SOFTWAREJOB"],
    )
    before = path.read_bytes()
    report = WRITER.run(root, release, True, root / "artifacts/software.json")
    assert report["status"] == "NO_CHANGE"
    assert path.read_bytes() == before
    return 1


def main():
    """Check actual writer code with mock evidence, never with panel actions."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with TemporaryDirectory(prefix="usp-writer-") as directory:
        root = Path(directory)
        release = fixture(root)
        count = preview_apply_idempotence(root, release)
        count += current_release(root, release)
        count += same_version_software(root, release)
        concurrent_refusal(root)
    count += refusals() + backup_refusals() + 1
    logger.info("CHECKPOINT_WRITER_NATIVE=PASS cases={} device_contact=0", count)


if __name__ == "__main__":
    main()
