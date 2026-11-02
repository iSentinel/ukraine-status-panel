#!/usr/bin/env python3
"""Exercise recovery consistency using disposable files; no device access."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "audit_progress", ROOT / "scripts/audit-progress-state.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def write_json(path, value):
    """Write only disposable fixture evidence."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value) + "\n")


def frozen_files(frozen):
    """Create a bounded set of disposable frozen-image fixtures."""
    names = (
        "firmware.ota.bin",
        "firmware.factory.bin",
        "firmware.elf",
        "sdkconfig",
    )
    checksums = {}
    for name in names:
        (frozen / name).write_text("sealed " + name)
        checksums[name] = hashlib.sha256(
            (frozen / name).read_bytes()
        ).hexdigest()
    return checksums


def accepted_state(root, frozen, manifest):
    """Build authoritative mirrors for one disposable accepted release."""
    checksums = manifest["checksums"]
    state = dict(
        installed_version=manifest["version"],
        installed_device_version=manifest["version"],
        accepted_version=manifest["version"],
        accepted_at=manifest["accepted_at"],
        accepted_source_fingerprint=manifest["source_fingerprint"],
        accepted_ota_sha256=checksums["firmware.ota.bin"],
        hardware_qualification=manifest["hardware_qualification_status"],
        source_version="0.115.294-dev",
        production_fingerprint="a" * 64,
        source_fingerprint="b" * 64,
        frozen_image_sha256=checksums["firmware.ota.bin"],
        active_jobs=["UART1"],
        current_stage="REVIEW-293-ACCEPTED",
        current_task="0.115.293-dev accepted while .294 is checked",
    )
    state["latest_hardware_checkpoint"] = dict(
        version=manifest["version"],
        source_fingerprint=manifest["source_fingerprint"],
        ota_sha256=checksums["firmware.ota.bin"],
    )
    state["accepted_candidates"] = [dict(version=manifest["version"])]
    state["frozen_image"] = dict(
        version=manifest["version"],
        path=str((frozen / "firmware.ota.bin").relative_to(root)),
        sha256=checksums["firmware.ota.bin"],
        source_fingerprint=manifest["source_fingerprint"],
    )
    return state


def fixture(root):
    """Represent an accepted image while a newer candidate is being checked."""
    base = root / "artifacts/0.115.293-test"
    frozen = base / "frozen"
    frozen.mkdir(parents=True)
    manifest = dict(
        version="0.115.293-dev",
        validation="hardware_accepted",
        source_fingerprint="a" * 64,
        checksums=frozen_files(frozen),
        accepted_at="2026-01-01T00:00:00+00:00",
        hardware_qualification_status="PASS_TEST",
    )
    write_json(base / "frozen-manifest.json", manifest)
    candidate = dict(
        manifest, version="0.115.294-dev", validation="frozen_not_uploaded"
    )
    write_json(
        root / "artifacts/0.115.294-test/frozen-manifest.json", candidate
    )
    state = accepted_state(root, frozen, manifest)
    write_json(root / ".work-state/firmware-progress.json", state)
    (root / "firmware").mkdir()
    (root / "firmware/ukraine-status-panel.yaml").write_text(
        f'substitutions:\n  firmware_version: "{candidate["version"]}"\n'
    )
    return state, frozen


def accepted_validation_case(root):
    """Recognize the explicit accepted-with-deferred-hardware status."""
    base = root / "artifacts/0.115.295-test"
    frozen = base / "frozen"
    frozen.mkdir(parents=True)
    manifest = dict(
        version="0.115.295-dev",
        validation="hardware_accepted_shelly_sht_deferred_optional",
        source_fingerprint="d" * 64,
        checksums=frozen_files(frozen),
    )
    write_json(base / "frozen-manifest.json", manifest)
    _path, accepted = MODULE.latest_accepted(root)
    assert accepted["version"] == "0.115.295-dev"
    return 1


def drift_cases(root, state):
    """Detect each stale pointer independently, including the original .286 error."""
    path = root / ".work-state/firmware-progress.json"
    cases = 0
    for key in (
        "installed_version",
        "installed_device_version",
        "accepted_version",
        "accepted_at",
        "accepted_source_fingerprint",
        "accepted_ota_sha256",
        "hardware_qualification",
        "source_version",
        "production_fingerprint",
        "frozen_image_sha256",
        "frozen_image",
    ):
        bad = dict(state)
        bad[key] = "0.115.286-dev"
        write_json(path, bad)
        assert MODULE.audit(root)["status"] == "FAIL", key
        cases += 1
    write_json(path, state)
    assert MODULE.audit(root, fingerprint="c" * 64)["status"] == "FAIL"
    assert MODULE.audit(root, running_jobs=["UNRECORDED"])["status"] == "FAIL"
    assert (
        MODULE.audit(root, fingerprint="b" * 64, running_jobs=["UART1"])[
            "status"
        ]
        == "PASS"
    )
    return cases + 3


def active_pointer_cases(root, state):
    """Reject stale accepted history and active task/stage mirrors."""
    path = root / ".work-state/firmware-progress.json"
    cases = 0
    mutations = (
        {"latest_hardware_checkpoint": {"version": "0.115.292-dev"}},
        {"accepted_candidates": []},
        {"current_task": "0.115.292-dev accepted"},
        {"current_stage": "REVIEW-292-ACCEPTED"},
    )
    for values in mutations:
        bad = dict(state)
        bad.update(values)
        write_json(path, bad)
        assert MODULE.audit(root)["status"] == "FAIL", values
        cases += 1
    write_json(path, state)
    return cases


def current_file_cases(root, state):
    """Reject a stale CURRENT mirror once source equals accepted baseline."""
    state_path = root / ".work-state/firmware-progress.json"
    firmware = root / "firmware/ukraine-status-panel.yaml"
    original_firmware = firmware.read_text()
    current = root / ".work-state/CURRENT"
    accepted = dict(state, source_version="0.115.293-dev")
    accepted["current_task_status"] = "PASS"
    write_json(state_path, accepted)
    firmware.write_text(original_firmware.replace("0.115.294-dev", "0.115.293-dev"))
    message = accepted["current_task"]
    current.write_text(
        f"stage=REVIEW-293-ACCEPTED\nstatus=PASS\nmessage={message}\n"
    )
    assert MODULE.audit(root)["status"] == "PASS"
    current.write_text(
        f"stage=REVIEW-292-ACCEPTED\nstatus=PASS\nmessage={message}\n"
    )
    assert MODULE.audit(root)["status"] == "FAIL"
    current.write_text(
        "stage=REVIEW-293-ACCEPTED\nstatus=PASS\nmessage=stale task\n"
    )
    assert MODULE.audit(root)["status"] == "FAIL"
    current.unlink()
    firmware.write_text(original_firmware)
    write_json(state_path, state)
    return 3


def owner_file_cases(root, state):
    """Check only the active top-level owner, not historical owners."""
    path = root / ".work-state/OWNER.json"
    owner = dict(
        status="ACTIVE_MAINTENANCE_NO_SOURCE_CHANGE",
        current_stage=state["current_stage"],
        note=state["current_task"],
        previous_owner={"status": "PENDING", "current_stage": "OLD"},
    )
    write_json(path, owner)
    assert MODULE.audit(root)["status"] == "PASS"
    owner["current_stage"] = "STALE-STAGE"
    write_json(path, owner)
    assert MODULE.audit(root)["status"] == "FAIL"
    owner["current_stage"] = state["current_stage"]
    owner["note"] = "stale task"
    write_json(path, owner)
    assert MODULE.audit(root)["status"] == "FAIL"
    owner["status"] = "PENDING"
    write_json(path, owner)
    assert MODULE.audit(root)["status"] == "PASS"
    path.unlink()
    return 4


def operation_cases(root, state):
    """Reject false zero-attempt recovery after real log/job evidence exists."""
    directory = root / "artifacts/0.115.294-test"
    for kind, log in (("build", "production-build1.log"), ("ota", "ota1.log")):
        (directory / log).write_text("Recorded attempt")
        (directory / (kind + "-job.txt")).write_text(kind + "1")
    path = root / ".work-state/firmware-progress.json"
    pending = dict(
        state, current_operation={"build_attempts": 0, "ota_attempts": 0}
    )
    write_json(path, pending)
    assert MODULE.audit(root, operation_dir=directory)["status"] == "FAIL"
    pending["current_operation"] = {
        "build_attempts": 1,
        "ota_attempts": 1,
        "build_job": "build1",
        "ota_job": "ota1",
    }
    write_json(path, pending)
    assert MODULE.audit(root, operation_dir=directory)["status"] == "PASS"
    pending["current_operation"]["ota_job"] = "WRONG"
    write_json(path, pending)
    assert MODULE.audit(root, operation_dir=directory)["status"] == "FAIL"
    assert (
        MODULE.audit(root, operation_dir=root / "missing")["status"] == "FAIL"
    )
    write_json(path, state)
    return 4


def isolated_log_cases(root, state):
    """Detect zero counters for each real or historical log name alone."""
    path = root / ".work-state/firmware-progress.json"
    cases = 0
    for index, (kind, name) in enumerate(
        (
            ("build", "production-build.log"),
            ("build", "production-build1.log"),
            ("ota", "ota1.log"),
        )
    ):
        for payload in ("", "Recorded attempt"):
            directory = root / f"artifacts/log-only-{index}-{len(payload)}"
            directory.mkdir()
            (directory / name).write_text(payload)
            pending = dict(state, current_operation={kind + "_attempts": 0})
            write_json(path, pending)
            assert (
                MODULE.audit(root, operation_dir=directory)["status"] == "FAIL"
            ), name
            pending["current_operation"][kind + "_attempts"] = 1
            write_json(path, pending)
            assert (
                MODULE.audit(root, operation_dir=directory)["status"] == "PASS"
            ), name
            cases += 2
    write_json(path, state)
    return cases


def main():
    """Check drift, corrupt images, version order and read-only behavior."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    assert MODULE.version_key("0.115.100-dev") > MODULE.version_key(
        "0.115.99-dev"
    )
    with TemporaryDirectory(prefix="usp-progress-") as directory:
        root = Path(directory)
        state, frozen = fixture(root)
        before = (root / ".work-state/firmware-progress.json").read_bytes()
        assert MODULE.audit(root)["status"] == "PASS"
        assert (
            root / ".work-state/firmware-progress.json"
        ).read_bytes() == before
        cases = drift_cases(root, state) + active_pointer_cases(root, state)
        cases += current_file_cases(root, state)
        cases += owner_file_cases(root, state) + operation_cases(root, state)
        cases += isolated_log_cases(root, state)
        cases += accepted_validation_case(root)
        device = root / "device.json"
        write_json(device, dict(version="0.115.292-dev"))
        assert MODULE.audit(root, device=device)["status"] == "FAIL"
        (frozen / "firmware.ota.bin").write_text("corrupt")
        assert MODULE.audit(root)["status"] == "FAIL"
        (frozen / "firmware.ota.bin").unlink()
        (frozen / "firmware.ota.bin").symlink_to(root / "device.json")
        assert MODULE.audit(root)["status"] == "FAIL"
    logger.info(
        "PROGRESS_STATE_NATIVE=PASS cases={} no panel access", cases + 5
    )


if __name__ == "__main__":
    main()
