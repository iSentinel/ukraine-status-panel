#!/usr/bin/env python3
"""Check saved firmware progress against local source and frozen evidence.

This tool reads files only. It cannot build, upload, reset or repair state.
Device JSON is earlier read-only evidence, not a new connection to the panel.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

from loguru import logger


ACCEPTED_VALIDATIONS = {
    "hardware_accepted",
    "hardware_accepted_shelly_sht_deferred_optional",
}


def read_json(path):
    """Read one object without accepting a list as a checkpoint."""
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return value


def version_key(text):
    """Order complete project versions without comparing decimal strings."""
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)-dev", text)
    if not match:
        raise ValueError(f"Unsupported firmware version: {text}")
    return tuple(int(value) for value in match.groups())


def latest_accepted(root):
    """Select the newest sealed acceptance, ignoring unfinished candidates."""
    records = []
    for path in (root / "artifacts").glob("0.115.*/frozen-manifest.json"):
        value = read_json(path)
        if value.get("validation") in ACCEPTED_VALIDATIONS:
            records.append((version_key(value["version"]), path, value))
    if not records:
        raise ValueError("No hardware-accepted frozen manifest was found")
    _, path, value = max(records, key=lambda row: row[0])
    return path, value


def source_version(root):
    """Read only the firmware version, without printing configuration secrets."""
    text = (root / "firmware/ukraine-status-panel.yaml").read_text()
    match = re.search(r'^  firmware_version: "([^"]+)"$', text, re.MULTILINE)
    if not match:
        raise ValueError("Canonical firmware version is missing")
    version_key(match[1])
    return match[1]


def frozen_issues(path, manifest):
    """Verify every sealed file and refuse paths outside its frozen directory."""
    issues = []
    frozen = (path.parent / "frozen").resolve()
    names = {
        "firmware.ota.bin",
        "firmware.factory.bin",
        "firmware.elf",
        "sdkconfig",
    }
    checksums = manifest.get("checksums", {})
    if set(checksums) != names:
        return [
            "Accepted manifest has an incomplete or unexpected checksum set"
        ]
    for name, wanted in checksums.items():
        file = (frozen / name).resolve()
        if not file.is_relative_to(frozen):
            issues.append(f"Frozen file leaves the evidence directory: {name}")
            continue
        try:
            with file.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != wanted:
                issues.append(f"Frozen checksum mismatch: {name}")
        except OSError:
            issues.append(f"Frozen file cannot be read: {name}")
    return issues


def accepted_pointer_issues(state, manifest):
    """Check mirrored accepted pointers that must track the sealed release."""
    sha256 = manifest["checksums"]["firmware.ota.bin"]
    expected = {
        "accepted_version": manifest["version"],
        "accepted_source_fingerprint": manifest["source_fingerprint"],
        "accepted_ota_sha256": sha256,
        "installed_device_version": manifest["version"],
    }
    if manifest.get("accepted_at"):
        expected["accepted_at"] = manifest["accepted_at"]
    if manifest.get("hardware_qualification_status"):
        expected["hardware_qualification"] = manifest["hardware_qualification_status"]
    issues = [
        f"Checkpoint {key} differs from verified accepted evidence"
        for key, value in expected.items()
        if state.get(key) != value
    ]
    latest = state.get("latest_hardware_checkpoint")
    if latest is not None and not isinstance(latest, dict):
        issues.append("Checkpoint latest_hardware_checkpoint is not an object")
    elif latest is not None:
        for key, value in (
            ("version", manifest["version"]),
            ("source_fingerprint", manifest["source_fingerprint"]),
            ("ota_sha256", sha256),
        ):
            if latest.get(key) != value:
                issues.append(f"Latest hardware checkpoint {key} is stale")
    records = state.get("accepted_candidates")
    if records is not None and (
        not isinstance(records, list)
        or not any(
            isinstance(item, dict) and item.get("version") == manifest["version"]
            for item in records
        )
    ):
        issues.append("Latest accepted release is missing from accepted_candidates")
    return issues


def active_pointer_issues(state, manifest):
    """Reject active task/stage text that points only at an older release."""
    issues = []
    version = manifest["version"]
    task = state.get("current_task")
    if isinstance(task, str):
        references = re.findall(r"0\.115\.\d+-dev", task)
        if references and version not in references:
            issues.append("Checkpoint current_task references an older release")
    stage = state.get("current_stage")
    if isinstance(stage, str) and "ACCEPTED" in stage.upper():
        patch = version.split(".")[-1].split("-")[0]
        numbers = re.findall(r"(?<!\d)(\d{3})(?!\d)", stage)
        if numbers and patch not in numbers:
            issues.append("Checkpoint current_stage references an older acceptance")
    return issues


def current_file_issues(root, state, manifest, source):
    """Check CURRENT against active JSON state when source equals accepted."""
    path = root / ".work-state/CURRENT"
    if source != manifest["version"] or not path.exists():
        return []
    values = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    expected = {
        "stage": state.get("current_stage"),
        "status": state.get("current_task_status"),
        "message": state.get("current_task"),
    }
    return [
        f"CURRENT {key} differs from firmware-progress.json"
        for key, value in expected.items()
        if value is not None and values.get(key) != value
    ]


def owner_file_issues(root, state):
    """Reject a stale top-level active recovery-owner stage pointer."""
    path = root / ".work-state/OWNER.json"
    if not path.exists():
        return []
    owner = read_json(path)
    status = owner.get("status")
    if not isinstance(status, str) or not status.startswith("ACTIVE"):
        return []
    issues = []
    stage = state.get("current_stage")
    if isinstance(stage, str) and owner.get("current_stage") != stage:
        issues.append("OWNER current_stage differs from firmware-progress.json")
    task = state.get("current_task")
    if isinstance(task, str) and owner.get("note") != task:
        issues.append("OWNER note differs from firmware-progress.json")
    return issues


def checkpoint_issues(state, manifest, source, manifest_path, root):
    """Compare all authoritative pointers; never rewrite ambiguous state."""
    expected = {
        "installed_version": manifest["version"],
        "source_version": source,
        "production_fingerprint": manifest["source_fingerprint"],
        "frozen_image_sha256": manifest["checksums"]["firmware.ota.bin"],
    }
    issues = [
        f"Checkpoint {key} differs from verified evidence"
        for key, value in expected.items()
        if state.get(key) != value
    ]
    image = state.get("frozen_image", {})
    expected_image = {
        "version": manifest["version"],
        "path": str(
            (manifest_path.parent / "frozen/firmware.ota.bin").relative_to(
                root
            )
        ),
        "sha256": manifest["checksums"]["firmware.ota.bin"],
        "source_fingerprint": manifest["source_fingerprint"],
    }
    if not isinstance(image, dict):
        return issues + ["Checkpoint frozen_image is not an object"]
    issues.extend(
        f"Checkpoint frozen_image.{key} differs from verified evidence"
        for key, value in expected_image.items()
        if image.get(key) != value
    )
    issues.extend(accepted_pointer_issues(state, manifest))
    issues.extend(active_pointer_issues(state, manifest))
    issues.extend(owner_file_issues(root, state))
    return issues


def operation_issues(state, directory):
    """Refuse zero attempts or wrong job pointers after a recorded dispatch."""
    if not directory.is_dir():
        return ["Operation evidence directory does not exist"]
    operation = state.get("current_operation", {})
    if not isinstance(operation, dict):
        return ["Checkpoint current_operation is not an object"]
    issues = []
    logs = (
        ("build", ("production-build.log", "production-build1.log")),
        ("ota", ("ota1.log",)),
    )
    for kind, log_names in logs:
        if any((directory / name).exists() for name in log_names):
            count = operation.get(kind + "_attempts", 0)
            if type(count) is not int or count < 1:
                issues.append(
                    f"Checkpoint {kind}_attempts omits an existing attempt"
                )
    for kind in ("build", "ota", "uart", "hardware"):
        path = directory / (kind + "-job.txt")
        if path.exists():
            job = path.read_text().strip()
            if operation.get(kind + "_job") != job:
                issues.append(
                    f"Checkpoint {kind}_job differs from operation evidence"
                )
    return issues


def audit(root, device=None, fingerprint=None, running_jobs=(), operation_dir=None):
    """Return a non-secret report, keeping source and installed versions separate."""
    root = root.resolve()
    state = read_json(root / ".work-state/firmware-progress.json")
    path, accepted = latest_accepted(root)
    source = source_version(root)
    issues = frozen_issues(path, accepted)
    if not issues:
        issues.extend(checkpoint_issues(state, accepted, source, path, root))
        issues.extend(current_file_issues(root, state, accepted, source))
    if (
        device is not None
        and read_json(device).get("version") != accepted["version"]
    ):
        issues.append(
            "Read-only device evidence differs from latest accepted image"
        )
    if (
        fingerprint is not None
        and state.get("source_fingerprint") != fingerprint
    ):
        issues.append(
            "Checkpoint source_fingerprint differs from supplied source evidence"
        )
    if operation_dir is not None:
        issues.extend(operation_issues(state, operation_dir))
    active = state.get("active_jobs", [])
    for job in running_jobs:
        if job not in active:
            issues.append(f"Known running job missing from checkpoint: {job}")
    return dict(
        status="PASS" if not issues else "FAIL",
        installed_accepted=accepted["version"],
        source_version=source,
        accepted_manifest=str(path.relative_to(root)),
        issues=issues,
        device_evidence_supplied=device is not None,
        physical_device_contact=False,
        state_modified=False,
    )


def arguments():
    """Describe optional evidence inputs and a new report output path."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument(
        "--operation-dir",
        type=Path,
        help="Current release evidence directory to check attempts and job pointers",
    )
    parser.add_argument(
        "--device-json", type=Path, help="Earlier read-only device snapshot"
    )
    parser.add_argument(
        "--source-fingerprint", help="Expected current source fingerprint"
    )
    parser.add_argument(
        "--running-job",
        action="append",
        default=[],
        help="Job confirmed running through Gateway; repeat as needed",
    )
    parser.add_argument(
        "--output", type=Path, help="New JSON report; overwriting is refused"
    )
    return parser.parse_args()


def main():
    """Exit 2 on inconsistent evidence so continuation cannot silently pass."""
    args = arguments()
    if args.output and args.output.exists():
        raise SystemExit(
            "Report exists; use a new output path to retain evidence"
        )
    try:
        report = audit(
            args.root,
            args.device_json,
            args.source_fingerprint,
            args.running_job,
            args.operation_dir,
        )
    except (OSError, ValueError, KeyError, TypeError) as error:
        logger.error("Progress evidence could not be verified: {}", error)
        raise SystemExit(2) from error
    if args.output:
        with args.output.open("x") as stream:
            stream.write(
                json.dumps(report, ensure_ascii=False, indent=2) + "\n"
            )
    logger.info("PROGRESS_STATE_AUDIT={} {}", report["status"], report)
    raise SystemExit(0 if report["status"] == "PASS" else 2)


if __name__ == "__main__":
    main()
