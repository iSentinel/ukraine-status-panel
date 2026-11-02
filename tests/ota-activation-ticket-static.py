#!/usr/bin/env python3
"""Static contract for compile-only one-time OTA activation tickets."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_activation_ticket.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_activation_ticket.cpp").read_text(encoding="utf-8")
COMBINED = HEADER + SOURCE

PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")
if CURRENT_FIRMWARE_MARKER not in PROD:
    raise SystemExit("wrong production version")
if CURRENT_FIRMWARE_MARKER not in PUBLIC:
    raise SystemExit("wrong public version")

required = (
    "TICKET_BYTES = 16",
    "SESSION_BINDING_BYTES = 16",
    "SHA256_BYTES = 32",
    "TICKET_LIFETIME_MS = 30U * 1000U",
    "esp_fill_random",
    "sodium_memcmp",
    "sodium_memzero",
    "ActivationEligibility::ELIGIBLE",
    "ActivationBlockReason::NONE",
    "SESSION_MISMATCH",
    "TARGET_MISMATCH",
    "DIGEST_MISMATCH",
    "VERSION_MISMATCH",
    "ActivationTicketState::CONSUMED",
    "ActivationTicketState::EXPIRED",
    "now_ms - this->started_ms_",
)
missing = [token for token in required if token not in COMBINED]
if missing:
    raise SystemExit(f"missing activation-ticket tokens: {missing}")

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
    raise SystemExit(f"activation ticket contains mutating tokens: {leaked}")

runtime_files = [
    COMP / "ota_guard.cpp",
    COMP / "ota_guard.h",
    COMP / "__init__.py",
    COMP / "ota_write_pipeline.cpp",
    COMP / "ota_flash_backend.cpp",
    COMP / "ota_activation_policy.cpp",
]
wiring = "".join(path.read_text(encoding="utf-8") for path in runtime_files)
for token in ("ota_activation_ticket", "OtaActivationTicket"):
    if token in wiring:
        raise SystemExit(f"activation ticket is wired into runtime path: {token}")

if "usp_ota_guard" in PUBLIC or "OtaActivationTicket" in PUBLIC:
    raise SystemExit("public first-flash unexpectedly contains private activation ticket")

print("OTA_ACTIVATION_TICKET_STATIC=PASS bits=128 ttl_ms=30000 one_time=1 bindings=4 unwired=1 no_activate=1")
