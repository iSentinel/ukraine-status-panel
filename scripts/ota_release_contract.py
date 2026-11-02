#!/usr/bin/env python3
"""Shared helpers for the private OTA release contract."""
from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import re
import struct

APP_DESC_OFFSET = 0x20
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_VERSION_OFFSET = APP_DESC_OFFSET + 16
APP_DESC_PROJECT_OFFSET = APP_DESC_OFFSET + 48
FIELD_LEN = 32
PROJECT_NAME = "ukraine-status-panel.panel"
CHIP_FAMILY = "ESP32-S3"


def scalar(text: str, key: str) -> str:
    """Read one scalar from the ESPHome YAML without evaluating secrets."""
    pattern = rf'^\s*{re.escape(key)}:\s*["\']?([^"\'\n]+)'
    match = re.search(pattern, text, re.M)
    if not match:
        raise RuntimeError(f"{key} not found")
    return match.group(1).strip()


def quoted_project(text: str) -> str:
    """Read esphome.project.name from the source YAML."""
    match = re.search(r'(?ms)^\s*project:\s*\n\s*name:\s*"([^"]+)"', text)
    if not match:
        raise RuntimeError("esphome project.name not found")
    return match.group(1)


def key_id(raw_public_key: bytes) -> str:
    """Return the stable identifier used by manifests and signatures."""
    digest = hashlib.sha256(raw_public_key).hexdigest()[:16]
    return f"ed25519:{digest}"


def load_trust(path: Path) -> tuple[dict[str, object], bytes]:
    """Load and validate the source-controlled Ed25519 trust anchor."""
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != 1 or data.get("algorithm") != "Ed25519":
        raise RuntimeError("unsupported OTA trust anchor")
    raw = base64.b64decode(str(data.get("public_key_base64", "")), validate=True)
    if len(raw) != 32:
        raise RuntimeError("Ed25519 public key must be 32 bytes")
    if data.get("key_id") != key_id(raw):
        raise RuntimeError("OTA trust-anchor key_id mismatch")
    return data, raw


def c_string(blob: bytes, offset: int, length: int = FIELD_LEN) -> str:
    """Decode one fixed-width C string from an ESP app descriptor."""
    end = offset + length
    return blob[offset:end].split(b"\0", 1)[0].decode("utf-8", errors="strict")


def inspect_image(path: Path, device: str, project: str, version: str) -> dict[str, object]:
    """Validate basic ESP image identity and return exact integrity metadata."""
    blob = path.read_bytes()
    if len(blob) < 512 or blob[0] != 0xE9:
        raise RuntimeError("not an ESP application image")
    magic = struct.unpack_from("<I", blob, APP_DESC_OFFSET)[0]
    if magic != APP_DESC_MAGIC:
        raise RuntimeError("ESP app descriptor magic missing at 0x20")
    descriptor_project = c_string(blob, APP_DESC_PROJECT_OFFSET)
    if descriptor_project != device:
        raise RuntimeError(
            f"app descriptor project mismatch: {descriptor_project!r} != {device!r}")
    for token, label in ((project, "project"), (version, "firmware version")):
        if token.encode("utf-8") not in blob:
            raise RuntimeError(f"embedded {label} marker missing: {token}")
    return {
        "kind": "ota",
        "filename": "firmware.ota.bin",
        "size": len(blob),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "esp_app_desc": {
            "project_name": descriptor_project,
            "framework_version": c_string(blob, APP_DESC_VERSION_OFFSET),
        },
    }


def ensure_private_profile(config: Path, text: str) -> None:
    """Reject public/provisioning profiles and incomplete rollback policy."""
    if "public-first-flash" in config.name or "name_add_mac_suffix: true" in text:
        raise RuntimeError("public first-flash profile must never be used as an update image")
    required = (
        ("enable_ota_rollback: true", "ESP-IDF OTA rollback"),
        ("safe_mode:", "safe mode"),
        ("ota: false", "disabled browser OTA"),
        ("web_server_password", "private authenticated Web profile"),
    )
    for token, label in required:
        if token not in text:
            raise RuntimeError(f"private OTA source missing {label}")


def build_manifest(config: Path, image: Path, trust_path: Path) -> dict[str, object]:
    """Build schema-v2 metadata for one exact private-production OTA image."""
    text = config.read_text(encoding="utf-8")
    ensure_private_profile(config, text)
    trust, _ = load_trust(trust_path)
    version = scalar(text, "firmware_version")
    device = scalar(text, "device_name")
    project = quoted_project(text)
    image_info = inspect_image(image, device, project, version)
    return {
        "schema": 2,
        "product": "Ukraine Status Panel",
        "profile": "private-production",
        "project": project,
        "device": device,
        "chip_family": CHIP_FAMILY,
        "flash_size": scalar(text, "flash_size"),
        "version": version,
        "image": image_info,
        "trust": {
            "algorithm": "Ed25519",
            "key_id": trust["key_id"],
            "signature_required": True,
        },
        "update_policy": {
            "downgrade_default": "deny",
            "same_version": "allow",
            "same_project_required": True,
            "same_chip_family_required": True,
        },
        "safety": {
            "esp_idf_rollback": True,
            "safe_mode": True,
            "browser_ota_enabled": False,
            "signed_image_verification": True,
        },
        "distribution": {
            "public": False,
            "contains_private_profile": True,
            "warning": "LOCAL USE ONLY: do not publish or share this OTA image.",
        },
    }
