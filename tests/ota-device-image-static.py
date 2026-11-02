#!/usr/bin/env python3
"""Guard the streaming OTA image preflight and its no-write boundary."""
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
        "begin_image_preflight", "add_image_chunk", "finish_image_preflight",
        "reset_image_preflight", "mbedtls_sha256_starts", "mbedtls_sha256_update",
        "mbedtls_sha256_finish", "IMAGE_PREFIX_BYTES", "0xABCD5432U", "0xE9",
        "FRAMEWORK_VERSION", "DEVICE_NAME", "SIZE_OVERFLOW", "SIZE_MISMATCH",
        "HEADER_MISMATCH", "APP_DESC_MISMATCH", "SHA256_MISMATCH",
    ):
        need(token in CPP or token in H, f"stream image preflight missing: {token}")
    need("std::array<uint8_t, IMAGE_PREFIX_BYTES>" in H,
         "full image must not be buffered")
    need("expected_image_size_" in H and "expected_image_sha256_" in H,
         "manifest result is not bound to stream")
    need("usp_ota_guard" not in PUBLIC, "public first-flash leaked OTA guard")
    forbidden = (
        "esp_ota_begin", "esp_ota_write", "esp_ota_end", "esp_ota_set_boot_partition",
        "esp_restart",
    )
    combined = CPP + H
    leaked = [token for token in forbidden if token in combined]
    need(not leaked, f"stream preflight crossed no-write/no-route boundary: {leaked}")
    print("OTA_DEVICE_IMAGE_STATIC=PASS streaming_sha256=1 no_writer=1")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_DEVICE_IMAGE_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
