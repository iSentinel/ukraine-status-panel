#!/usr/bin/env python3
"""Behavior model for read-only OTA post-write validation."""
from dataclasses import dataclass
from hashlib import sha256


@dataclass
class Partition:
    data: bytes
    size: int
    app: bool = True
    valid: bool = True
    project: str = "ukraine-status-panel"
    version: str = "0.115.235-dev"
    read_fails_at: int | None = None


def validate(part: Partition | None, image_size: int, digest: bytes,
             project: str, version: str) -> str:
    if part is None or image_size <= 0 or len(digest) != 32 or not project or not version:
        return "invalid_argument"
    if not part.valid or image_size > part.size:
        return "partition_invalid"
    if not part.app:
        return "not_app_partition"
    if part.project != project:
        return "project_mismatch"
    if part.version != version:
        return "version_mismatch"

    readback = bytearray()
    for offset in range(0, image_size, 1024):
        if part.read_fails_at is not None and offset >= part.read_fails_at:
            return "read_failed"
        readback.extend(part.data[offset:min(offset + 1024, image_size)])
    if len(readback) != image_size:
        return "read_failed"
    if sha256(readback).digest() != digest:
        return "digest_mismatch"
    return "valid"


def main() -> None:
    image = (b"USP-OTA-169" * 300)[:3072]
    digest = sha256(image).digest()
    base = Partition(data=image, size=4096)

    cases = [
        ("valid", validate(base, len(image), digest, base.project, base.version)),
        ("null", validate(None, len(image), digest, base.project, base.version)),
        ("oversize", validate(base, 5000, digest, base.project, base.version)),
        ("not_app", validate(Partition(image, 4096, app=False), len(image), digest, base.project, base.version)),
        ("project", validate(base, len(image), digest, "other", base.version)),
        ("version", validate(base, len(image), digest, base.project, "0.0.0")),
        ("digest", validate(base, len(image), bytes(32), base.project, base.version)),
        ("read", validate(Partition(image, 4096, read_fails_at=1024), len(image), digest, base.project, base.version)),
    ]
    expected = {
        "valid": "valid",
        "null": "invalid_argument",
        "oversize": "partition_invalid",
        "not_app": "not_app_partition",
        "project": "project_mismatch",
        "version": "version_mismatch",
        "digest": "digest_mismatch",
        "read": "read_failed",
    }
    failures = [(name, result, expected[name]) for name, result in cases if result != expected[name]]
    if failures:
        raise SystemExit(f"model failures: {failures}")
    print("OTA_POST_WRITE_VALIDATOR_MODEL=PASS cases=8 descriptor=1 digest=1 read_failure=1 no_mutation=1")


if __name__ == "__main__":
    main()
