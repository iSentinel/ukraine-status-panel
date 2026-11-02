#!/usr/bin/env python3
"""Guard the signed manifest device preflight and its no-write boundary."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text(encoding="utf-8")
H = (ROOT / "components/usp_ota_guard/ota_guard.h").read_text(encoding="utf-8")
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        "ManifestValidationResult", "validate_signed_manifest", "ManifestStatus::BAD_SIGNATURE",
        '"Ukraine Status Panel"', '"private-production"', '"ukraine-status-panel.panel"',
        '"ukraine-status-panel"', '"ESP32-S3"', '"16MB"', '"Ed25519"',
        '"ed25519:10ac93051fa3c7d5"', '"signature_required"', '"downgrade_default"',
        '"same_version"', '"same_project_required"', '"same_chip_family_required"',
        '"esp_idf_rollback"', '"safe_mode"', '"browser_ota_enabled"',
        '"signed_image_verification"', '"firmware.ota.bin"', '"2026.9.1"',
        "MAX_MANIFEST_BYTES", "MAX_OTA_IMAGE_BYTES", "valid_sha256",
    ):
        need(token in CPP or token in H, f"device manifest contract missing: {token}")
    verify_pos = CPP.find("this->verify_detached(manifest")
    parse_pos = CPP.find("json::parse_json(body)")
    need(verify_pos >= 0 and parse_pos >= 0 and verify_pos < parse_pos,
         "JSON is parsed before Ed25519 authentication")
    need("allow_downgrade = false" in H, "downgrade must default to denied")
    need("ManifestStatus::DOWNGRADE_DENIED" in CPP, "downgrade denial status missing")
    need("usp_ota_guard" not in PUBLIC, "public first-flash leaked OTA guard")
    forbidden = (
        "esp_ota_begin", "esp_ota_write", "esp_ota_end", "esp_ota_set_boot_partition",
        "esp_restart",
    )
    combined = CPP + H
    leaked = [token for token in forbidden if token in combined]
    need(not leaked, f"manifest preflight crossed no-write/no-route boundary: {leaked}")
    print("OTA_DEVICE_MANIFEST_STATIC=PASS signature_before_json=1")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_DEVICE_MANIFEST_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
