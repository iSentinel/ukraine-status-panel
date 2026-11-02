#!/usr/bin/env python3
"""Static contract for the hard-disabled rollback-confirm backend."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_rollback_confirm_backend.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_rollback_confirm_backend.cpp").read_text(encoding="utf-8")
COMBINED = HEADER + SOURCE
GUARD = (
    (COMP / "ota_guard.h").read_text(encoding="utf-8")
    + (COMP / "ota_guard.cpp").read_text(encoding="utf-8")
    + (COMP / "__init__.py").read_text(encoding="utf-8")
)
PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (
    ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml"
).read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    need(CURRENT_FIRMWARE_MARKER in PUBLIC, "wrong public version")

    for token in (
        "esp_ota_get_running_partition",
        "esp_partition_verify",
        "esp_ota_get_boot_partition",
        "esp_ota_get_state_partition",
        "esp_ota_mark_app_valid_cancel_rollback",
        "ESP_OTA_IMG_PENDING_VERIFY",
        "ESP_OTA_IMG_VALID",
        "ALREADY_VALID",
        "MARK_VALID_FAILED",
        "VERIFY_STATE_FAILED",
    ):
        need(token in COMBINED, f"missing rollback-confirm token: {token}")

    need(COMBINED.count("esp_ota_mark_app_valid_cancel_rollback") == 1,
         "mark-valid primitive must appear exactly once")
    need('bool runtime_enabled_{false};' in HEADER,
         "rollback-confirm backend is not hard-disabled")
    need("runtime_enabled_ = true" not in COMBINED,
         "rollback-confirm backend contains an enable assignment")
    need("set_runtime" not in COMBINED.lower(),
         "rollback-confirm backend exposes runtime-enable setter")

    confirm_pos = SOURCE.find("bool OtaRollbackConfirmBackend::confirm")
    gate_pos = SOURCE.find("if (!this->runtime_enabled_)", confirm_pos)
    running_pos = SOURCE.find("esp_ota_get_running_partition", confirm_pos)
    state_pos = SOURCE.find("esp_ota_get_state_partition", running_pos)
    valid_pos = SOURCE.find("current_state == ESP_OTA_IMG_VALID", state_pos)
    pending_pos = SOURCE.find("current_state != ESP_OTA_IMG_PENDING_VERIFY", valid_pos)
    mark_pos = SOURCE.find("esp_ota_mark_app_valid_cancel_rollback", pending_pos)
    readback_pos = SOURCE.find("esp_ota_get_state_partition", mark_pos)
    need(confirm_pos >= 0 and gate_pos > confirm_pos and running_pos > gate_pos,
         "hard-disabled gate must precede all partition/state calls")
    need(state_pos > running_pos and valid_pos > state_pos and pending_pos > valid_pos,
         "state/idempotence ordering is wrong")
    need(mark_pos > pending_pos and readback_pos > mark_pos,
         "mark-valid/readback ordering is wrong")

    forbidden = (
        "esp_ota_mark_app_invalid_rollback_and_reboot",
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
    need(not leaked, f"rollback-confirm backend contains forbidden tokens: {leaked}")

    for token in ("ota_rollback_confirm_backend", "OtaRollbackConfirmBackend"):
        need(token not in GUARD, f"rollback-confirm backend wired into runtime guard: {token}")
    need("usp_ota_guard" not in PUBLIC and "OtaRollbackConfirmBackend" not in PUBLIC,
         "private rollback-confirm backend leaked into public first-flash")

    global_instance = re.search(
        r"(?m)^\s*(?:static\s+)?OtaRollbackConfirmBackend\s+[A-Za-z_]\w*\s*[;{(]",
        SOURCE,
    )
    need(global_instance is None, "global/static rollback-confirm backend instance exists")

    print(
        "OTA_ROLLBACK_CONFIRM_BACKEND_STATIC=PASS mark_valid=1 readback=1 "
        "already_valid_idempotent=1 pending_only=1 hard_disabled=1 unwired=1 "
        "no_invalid_rollback=1 no_reboot=1 no_writer=1 public_isolated=1"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_ROLLBACK_CONFIRM_BACKEND_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
