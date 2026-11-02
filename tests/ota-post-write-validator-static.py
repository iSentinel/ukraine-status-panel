#!/usr/bin/env python3
"""Static contract for the compile-only read-only OTA post-write validator."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = COMP / "ota_post_write_validator.h"
SOURCE = COMP / "ota_post_write_validator.cpp"
GUARD = (COMP / "ota_guard.cpp").read_text(encoding="utf-8")
GUARD_H = (COMP / "ota_guard.h").read_text(encoding="utf-8")
INIT = (COMP / "__init__.py").read_text(encoding="utf-8")
PIPE = (COMP / "ota_write_pipeline.cpp").read_text(encoding="utf-8")

header = HEADER.read_text(encoding="utf-8")
source = SOURCE.read_text(encoding="utf-8")
combined = header + source

PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")
if CURRENT_FIRMWARE_MARKER not in PROD:
    raise SystemExit("wrong production version")
if CURRENT_FIRMWARE_MARKER not in PUBLIC:
    raise SystemExit("wrong public version")

required = (
    "esp_partition_verify(partition)",
    "esp_ota_get_partition_description",
    "esp_partition_read",
    "mbedtls_sha256_starts",
    "mbedtls_sha256_update",
    "mbedtls_sha256_finish",
    "constant_time_equal_",
    "PROJECT_MISMATCH",
    "VERSION_MISMATCH",
    "DIGEST_MISMATCH",
)
missing = [token for token in required if token not in combined]
if missing:
    raise SystemExit(f"missing validator contract tokens: {missing}")

forbidden = (
    "esp_ota_begin",
    "esp_ota_write",
    "esp_ota_end",
    "esp_ota_abort",
    "esp_ota_set_boot_partition",
    "esp_partition_write",
    "esp_partition_write_raw",
    "esp_partition_erase_range",
    "esp_restart",
)
leaked = [token for token in forbidden if token in combined]
if leaked:
    raise SystemExit(f"read-only validator contains mutating tokens: {leaked}")

wiring = GUARD + GUARD_H + INIT + PIPE
for token in ("ota_post_write_validator", "OtaPostWriteValidator"):
    if token in wiring:
        raise SystemExit(f"validator is wired into runtime path: {token}")

public_yaml = (ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")
if "usp_ota_guard" in public_yaml or "OtaPostWriteValidator" in public_yaml:
    raise SystemExit("public first-flash unexpectedly contains private OTA validator")

print("OTA_POST_WRITE_VALIDATOR_STATIC=PASS read_only=1 full_sha256=1 descriptor=1 unwired=1 no_activate=1")
