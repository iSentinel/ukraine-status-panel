#!/usr/bin/env python3
"""Static contract for the hard-disabled compile-only boot activation backend."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_boot_activation_backend.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_boot_activation_backend.cpp").read_text(encoding="utf-8")
COMBINED = HEADER + SOURCE
GUARD = (
    (COMP / "ota_guard.h").read_text(encoding="utf-8")
    + (COMP / "ota_guard.cpp").read_text(encoding="utf-8")
    + (COMP / "__init__.py").read_text(encoding="utf-8")
)
PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    need(CURRENT_FIRMWARE_MARKER in PUBLIC, "wrong public version")

    for token in (
        "esp_partition_verify",
        "esp_ota_get_running_partition",
        "esp_ota_set_boot_partition",
        "esp_ota_get_boot_partition",
        "ESP_PARTITION_TYPE_APP",
        "TARGET_IS_RUNNING",
        "SET_BOOT_FAILED",
        "VERIFY_BOOT_FAILED",
    ):
        need(token in COMBINED, f"missing boot activation token: {token}")

    need(COMBINED.count("esp_ota_set_boot_partition") == 1,
         "boot-selection primitive must appear exactly once")
    need('bool runtime_enabled_{false};' in HEADER,
         "activation backend is not hard-disabled")
    need("runtime_enabled_ = true" not in COMBINED,
         "activation backend contains an enable assignment")
    need("set_runtime" not in COMBINED.lower(),
         "activation backend exposes runtime-enable setter")
    need("enable_runtime" not in COMBINED.lower(),
         "activation backend exposes runtime-enable method")

    select_pos = SOURCE.find("bool OtaBootActivationBackend::select")
    gate_pos = SOURCE.find("if (!this->runtime_enabled_)", select_pos)
    verify_pos = SOURCE.find("esp_partition_verify", select_pos)
    set_pos = SOURCE.find("esp_ota_set_boot_partition", select_pos)
    readback_pos = SOURCE.find("esp_ota_get_boot_partition", set_pos)
    need(select_pos >= 0 and gate_pos > select_pos and verify_pos > gate_pos,
         "hard-disabled gate must precede partition verification")
    need(set_pos > verify_pos and readback_pos > set_pos,
         "boot selection/readback ordering is wrong")

    forbidden = (
        "esp_restart",
        "esp_ota_mark_app_valid_cancel_rollback",
        "esp_ota_mark_app_invalid_rollback_and_reboot",
        "esp_ota_begin",
        "esp_ota_write",
        "esp_ota_end",
        "esp_ota_abort",
        "esp_partition_write",
        "esp_partition_write_raw",
        "esp_partition_erase_range",
    )
    leaked = [token for token in forbidden if token in COMBINED]
    need(not leaked, f"boot activation backend contains forbidden tokens: {leaked}")

    for token in ("ota_boot_activation_backend", "OtaBootActivationBackend"):
        need(token not in GUARD, f"boot activation backend wired into runtime guard: {token}")
    need("usp_ota_guard" not in PUBLIC and "OtaBootActivationBackend" not in PUBLIC,
         "private boot activation backend leaked into public first-flash")

    global_instance = re.search(
        r"(?:^|\n)\s*(?:static\s+)?OtaBootActivationBackend\s+[A-Za-z_]\w*\s*[;{(]",
        SOURCE,
    )
    need(global_instance is None, "global/static boot activation backend instance exists")

    print(
        "OTA_BOOT_ACTIVATION_BACKEND_STATIC=PASS set_boot=1 readback=1 "
        "hard_disabled=1 canonical_app_guard=1 running_guard=1 unwired=1 "
        "no_reboot=1 no_writer=1 public_isolated=1"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_BOOT_ACTIVATION_BACKEND_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
