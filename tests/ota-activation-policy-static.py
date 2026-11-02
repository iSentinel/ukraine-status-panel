#!/usr/bin/env python3
"""Static contract for compile-only OTA activation eligibility policy."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_activation_policy.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_activation_policy.cpp").read_text(encoding="utf-8")
COMBINED = HEADER + SOURCE

PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")
if CURRENT_FIRMWARE_MARKER not in PROD:
    raise SystemExit("wrong production version")
if CURRENT_FIRMWARE_MARKER not in PUBLIC:
    raise SystemExit("wrong public version")

required = (
    "esp_ota_get_running_partition",
    "esp_partition_verify",
    "TARGET_IS_RUNNING",
    "MANIFEST_NOT_VERIFIED",
    "IMAGE_NOT_VERIFIED",
    "WRITE_NOT_FINALIZED",
    "POST_WRITE_NOT_VALID",
    "SESSION_NOT_CURRENT",
    "PostWriteValidationState::VALID",
    "descriptor_ok",
    "digest_match",
)
missing = [token for token in required if token not in COMBINED]
if missing:
    raise SystemExit(f"missing activation policy tokens: {missing}")

forbidden = (
    "esp_ota_set_boot_partition",
    "esp_ota_begin",
    "esp_ota_write",
    "esp_ota_end",
    "esp_ota_abort",
    "esp_partition_write",
    "esp_partition_write_raw",
    "esp_partition_erase_range",
    "esp_restart",
)
leaked = [token for token in forbidden if token in COMBINED]
if leaked:
    raise SystemExit(f"eligibility policy contains mutating tokens: {leaked}")

runtime_files = [
    COMP / "ota_guard.cpp",
    COMP / "ota_guard.h",
    COMP / "__init__.py",
    COMP / "ota_write_pipeline.cpp",
    COMP / "ota_flash_backend.cpp",
]
wiring = "".join(path.read_text(encoding="utf-8") for path in runtime_files)
for token in ("ota_activation_policy", "OtaActivationPolicy"):
    if token in wiring:
        raise SystemExit(f"activation policy is wired into runtime path: {token}")

if "usp_ota_guard" in PUBLIC or "OtaActivationPolicy" in PUBLIC:
    raise SystemExit("public first-flash unexpectedly contains private activation policy")

print("OTA_ACTIVATION_POLICY_STATIC=PASS read_only=1 evidence_chain=1 running_partition_guard=1 unwired=1 no_activate=1")
