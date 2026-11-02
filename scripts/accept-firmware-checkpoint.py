#!/usr/bin/env python3
"""Reconcile accepted release pointers from settled local evidence.

No device, UART, network, build or OTA operation is performed.
The default is a preview; --apply requires a new backup under artifacts/.
"""

import argparse
from datetime import datetime
import importlib.util
import json
import os
from pathlib import Path
import tempfile

from loguru import logger


def audit_module(root):
    """Reuse version ordering and immutable image checks from the audit tool."""
    spec = importlib.util.spec_from_file_location(
        "progress_audit", root / "scripts/audit-progress-state.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_fingerprints(release, manifest):
    """Keep actual attempt counts tied to the compiled and observed source."""
    for name in (
        "build-before.txt",
        "build-after.txt",
        "hardware/final-fingerprint.txt",
    ):
        if (release / name).read_text().strip() != manifest["source_fingerprint"]:
            raise ValueError(f"Source fingerprint differs: {name}")
    if any(
        type(manifest.get(key)) is not int or manifest[key] < 1
        for key in ("build_attempts", "ota_attempts")
    ):
        raise ValueError("Actual build/OTA attempts are missing")


def verify_stages(release, manifest, gate_exit):
    """Require completed stages, device identity and equal source fingerprints."""
    names = (
        gate_exit,
        "production-compile.exit",
        "freeze.exit",
        "ota1.exit",
        "hardware/hardware1.exit",
    )
    for name in names:
        proof = (release / name).resolve()
        if (
            not proof.is_relative_to(release.resolve())
            or proof.read_text().strip() != "0"
        ):
            raise ValueError(f"Stage is not completed successfully: {name}")
    if (
        not (release / "hardware/hardware-uart-460800.status")
        .read_text()
        .startswith("PASS baud=460800")
    ):
        raise ValueError("UART evidence is not closed successfully")
    device = json.loads((release / "final-device.json").read_text())
    if not isinstance(device, dict):
        raise ValueError("Device evidence must be a JSON object")
    if (
        device.get("version") != manifest["version"]
        or type(device.get("services")) is not int
        or device.get("services") != 0
    ):
        raise ValueError("Device identity/services differ from accepted production")
    verify_fingerprints(release, manifest)
    return device


def accepted_evidence(root, release, gate_exit):
    """Require the latest accepted manifest and completed release evidence."""
    audit = audit_module(root)
    path, manifest = audit.latest_accepted(root)
    if path.parent.resolve() != release.resolve():
        raise ValueError("Release is not the latest hardware-accepted image")
    problems = audit.frozen_issues(path, manifest)
    if problems:
        raise ValueError("; ".join(problems))
    device = verify_stages(release, manifest, gate_exit)
    return audit, manifest, device


def refuse_pending_device(audit, state, manifest, source):
    """Do not replace newer observed or possibly updating device information."""
    accepted = audit.version_key(manifest["version"])
    if audit.version_key(source) < accepted:
        raise ValueError("Source is older than the accepted image")
    for key in ("installed_version", "device_version_last_verified"):
        value = state.get(key)
        if value and audit.version_key(value) > accepted:
            raise ValueError("A newer device version is awaiting acceptance")
    operation = state.get("current_operation", {})
    if (
        audit.version_key(source) > accepted
        and operation.get("status") in ("INTENT", "DISPATCHED")
        and operation.get("ota_attempts", 0)
    ):
        raise ValueError("A newer OTA may be in progress")


def accepted_checkpoint_summary(relative, manifest):
    """Build one canonical latest-checkpoint pointer from sealed evidence."""
    summary = dict(
        version=manifest["version"],
        artifact_dir=relative,
        manifest=relative + "/frozen-manifest.json",
        build_attempts=manifest["build_attempts"],
        ota_attempts=manifest["ota_attempts"],
        ota_sha256=manifest["checksums"]["firmware.ota.bin"],
        source_fingerprint=manifest["source_fingerprint"],
        status=manifest.get(
            "hardware_qualification_status", manifest["validation"]
        ),
    )
    receipt = manifest.get("hardware_qualification_receipt")
    if receipt:
        summary["hardware_receipt"] = relative + "/" + receipt
    return summary


def accepted_fields(root, release, manifest):
    """Build authoritative release pointers while retaining project history."""
    relative = str(release.relative_to(root))
    sha256 = manifest["checksums"]["firmware.ota.bin"]
    fields = dict(
        installed_version=manifest["version"],
        installed_device_version=manifest["version"],
        installed_status="hardware_accepted",
        accepted_version=manifest["version"],
        accepted_source_fingerprint=manifest["source_fingerprint"],
        accepted_ota_sha256=sha256,
        production_fingerprint=manifest["source_fingerprint"],
        frozen_image_sha256=sha256,
        frozen_image=dict(
            version=manifest["version"],
            path=relative + "/frozen/firmware.ota.bin",
            sha256=sha256,
            source_fingerprint=manifest["source_fingerprint"],
        ),
        device_version_last_verified=manifest["version"],
        device_version_evidence=relative + "/final-device.json",
    )
    if manifest.get("accepted_at"):
        fields["accepted_at"] = manifest["accepted_at"]
    qualification = manifest.get("hardware_qualification_status")
    if qualification:
        fields["hardware_qualification"] = qualification
    summary = accepted_checkpoint_summary(relative, manifest)
    fields["latest_hardware_checkpoint"] = summary
    software = dict(summary, status="hardware_accepted_after_software_gates")
    software.pop("hardware_receipt", None)
    fields["latest_software_checkpoint"] = software
    return fields


def update_acceptance_history(result, relative, manifest):
    """Ensure the latest accepted release has one durable history record."""
    entry = dict(
        version=manifest["version"],
        accepted_at=manifest.get("accepted_at"),
        build_attempts=manifest["build_attempts"],
        ota_attempts=manifest["ota_attempts"],
        ota_sha256=manifest["checksums"]["firmware.ota.bin"],
        source_fingerprint=manifest["source_fingerprint"],
    )
    receipt = manifest.get("hardware_qualification_receipt")
    if receipt:
        entry["hardware_summary"] = relative + "/" + receipt
    records = result.setdefault("accepted_candidates", [])
    for record in records:
        if isinstance(record, dict) and record.get("version") == entry["version"]:
            record.update({key: value for key, value in entry.items() if value})
            return
    records.append({key: value for key, value in entry.items() if value})


def complete_candidate(result, relative, manifest):
    """Close the matching source candidate without inventing new work."""
    candidate = result.setdefault("candidate", {})
    candidate.update(
        version=manifest["version"],
        artifact_dir=relative,
        status="hardware_accepted",
        validation=manifest["validation"],
        source_fingerprint=manifest["source_fingerprint"],
        frozen_image_sha256=manifest["checksums"]["firmware.ota.bin"],
        build_attempts=manifest["build_attempts"],
        ota_attempts=manifest["ota_attempts"],
        hardware_contact=manifest.get("hardware_contact", True),
    )
    mappings = (
        ("accepted_at", "accepted_at"),
        ("hardware_qualification_status", "hardware_qualification"),
        ("hardware_qualification_receipt", "hardware_receipt"),
        ("human_visual_review", "human_visual_review"),
    )
    for source_key, candidate_key in mappings:
        value = manifest.get(source_key)
        if value is not None:
            if source_key == "hardware_qualification_receipt":
                value = relative + "/" + value
            candidate[candidate_key] = value


def reconcile(root, release, state, audit, manifest):
    """Keep an independent newer source candidate and its jobs intact."""
    source = audit.source_version(root)
    refuse_pending_device(audit, state, manifest, source)
    result = json.loads(json.dumps(state))
    relative = str(release.relative_to(root))
    result.update(accepted_fields(root, release, manifest), source_version=source)
    update_acceptance_history(result, relative, manifest)
    if source == manifest["version"]:
        result["source_fingerprint"] = manifest["source_fingerprint"]
        complete_candidate(result, relative, manifest)
    complete_operation(root, release, state, result, manifest)
    return result


def complete_operation(root, release, state, result, manifest):
    """Finish only this release operation; preserve independent software work."""
    relative = str(release.relative_to(root))
    operation = state.get("current_operation", {})
    directory = operation.get("artifact_dir")
    patch = manifest["version"].split(".")[-1].split("-")[0]
    owned = (
        directory == relative
        if directory
        else state.get("current_stage", "").startswith("REVIEW-" + patch + "-")
    )
    if owned:
        output = result.setdefault("current_operation", {})
        output.update(
            status="PASS",
            build_attempts=manifest["build_attempts"],
            ota_attempts=manifest["ota_attempts"],
        )
        for kind in ("gate", "build", "ota", "uart", "hardware"):
            output[kind + "_job"] = manifest[kind + "_job"]
    completed = {manifest[key] for key in manifest if key.endswith("_job")}
    result["active_jobs"] = [
        job for job in result.get("active_jobs", []) if job not in completed
    ]


def persist(path, before, result, backup, root):
    """Back up before atomic replacement and refuse an observed concurrent edit."""
    if backup is None or not backup.resolve().is_relative_to(
        (root / "artifacts").resolve()
    ):
        raise ValueError("Apply requires a new backup path under project artifacts/")
    if path.read_text() != before:
        raise ValueError("Checkpoint changed concurrently; inspect before retry")
    with backup.open("x") as stream:
        stream.write(before)
        stream.flush()
        os.fsync(stream.fileno())
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", dir=path.parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, path.stat().st_mode & 0o777)
        if path.read_text() != before:
            raise ValueError("Checkpoint changed concurrently; backup retained")
        temporary.replace(path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def run(root, release, apply=False, backup=None, gate_exit="full-gates.exit"):
    """Preview by default; apply a proven repair exactly once when requested."""
    root, release = root.resolve(), release.resolve()
    if not release.is_relative_to(root / "artifacts"):
        raise ValueError("Release evidence must be under project artifacts/")
    audit, manifest, _ = accepted_evidence(root, release, gate_exit)
    path = root / ".work-state/firmware-progress.json"
    before = path.read_text()
    state = json.loads(before)
    result = reconcile(root, release, state, audit, manifest)
    changed = sorted(key for key in result if result.get(key) != state.get(key))
    status = "NO_CHANGE" if not changed else "PREVIEW"
    if apply and changed:
        result["updated_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
        persist(path, before, result, backup, root)
        status = "APPLIED"
    return dict(
        status=status,
        version=manifest["version"],
        changed_fields=changed,
        device_contact=False,
        build_or_ota_performed=False,
    )


def main():
    """Expose a reviewable filesystem repair without changing release evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--release-dir", type=Path, required=True)
    parser.add_argument("--gate-exit", default="full-gates.exit")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--backup", type=Path)
    args = parser.parse_args()
    try:
        report = run(
            args.root, args.release_dir, args.apply, args.backup, args.gate_exit
        )
    except (OSError, ValueError, KeyError, TypeError) as error:
        logger.error("Checkpoint repair refused: {}", error)
        raise SystemExit(2) from error
    logger.info("ACCEPTED_CHECKPOINT={} {}", report["status"], report)


if __name__ == "__main__":
    main()
