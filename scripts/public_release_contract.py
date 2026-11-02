"""Validate public metadata and bind publication to a declared accepted image.

The hardware declaration remains a reviewed repository input. This module
checks its exact image identity; it cannot perform hardware acceptance.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re


def metadata_errors(manifest: dict, status: dict, version: str) -> list[str]:
    """Check profile, installer and status without hardware access."""
    errors = []
    if manifest.get("name") != "Ukraine Status Panel":
        errors.append("unexpected manifest name")
    if manifest.get("version") != version:
        errors.append("manifest/public firmware version mismatch")
    builds = manifest.get("builds")
    valid_build = (
        isinstance(builds, list)
        and len(builds) == 1
        and isinstance(builds[0], dict)
        and builds[0].get("chipFamily") == "ESP32-S3"
    )
    if not valid_build:
        errors.append("manifest must contain one ESP32-S3 build")
    elif builds[0].get("parts") != [
        {"path": "firmware.factory.bin", "offset": 0}
    ]:
        errors.append(
            "factory image must be relative firmware.factory.bin at offset 0"
        )
    if type(status.get("schema")) is not int or status.get("schema") != 1:
        errors.append("unsupported release-status schema")
    if status.get("version") != manifest.get("version"):
        errors.append("release-status/manifest version mismatch")
    if type(status.get("public_first_flash_hardware_validated")) is not bool:
        errors.append("release-status hardware flag must be boolean")
    return errors


def image_identity(path: Path) -> tuple[int, str]:
    """Hash the actual file bytes without loading a factory image into RAM."""
    if not path.is_file():
        raise ValueError("factory image must be a regular file")
    size = 0
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    if not size:
        raise ValueError("factory image is empty")
    return size, digest.hexdigest()


def declaration_errors(manifest: dict, status: dict) -> list[str]:
    """Require a version, profile, hash, size and evidence declaration."""
    errors = []
    if status.get("public_first_flash_hardware_validated") is not True:
        errors.append("public first-flash hardware acceptance is not recorded")
    receipt = status.get("hardware_acceptance")
    if not isinstance(receipt, dict):
        return errors + ["exact-image hardware_acceptance receipt is missing"]
    if receipt.get("profile") != "public-first-flash":
        errors.append("hardware receipt is not for public-first-flash")
    if receipt.get("version") != manifest.get("version"):
        errors.append("hardware receipt/manifest version mismatch")
    wanted = receipt.get("factory_sha256")
    if not isinstance(wanted, str) or not re.fullmatch(
        r"[0-9a-f]{64}", wanted
    ):
        errors.append("hardware receipt needs lowercase factory SHA256")
    size = receipt.get("factory_size")
    if type(size) is not int or size <= 0:
        errors.append("hardware receipt needs positive factory size")
    evidence = receipt.get("evidence")
    if not isinstance(evidence, str) or not evidence.strip():
        errors.append(
            "hardware receipt needs an acceptance evidence reference"
        )
    return errors


def acceptance_errors(
    manifest: dict, status: dict, image: Path | None
) -> list[str]:
    """Reject every factory file except the exact declared accepted bytes."""
    errors = declaration_errors(manifest, status)
    if errors:
        return errors
    if image is None:
        return ["--factory-image is required for hardware publication"]
    receipt = status["hardware_acceptance"]
    actual_size, actual_sha = image_identity(image)
    if actual_size != receipt["factory_size"]:
        errors.append("factory image size differs from accepted image")
    if actual_sha != receipt["factory_sha256"]:
        errors.append("factory image SHA256 differs from accepted image")
    return errors
