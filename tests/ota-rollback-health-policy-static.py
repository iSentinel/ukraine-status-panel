#!/usr/bin/env python3
"""Static contract for the compile-only read-only rollback health policy."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_rollback_health_policy.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_rollback_health_policy.cpp").read_text(encoding="utf-8")
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
    need("MIN_STABLE_UPTIME_MS = 30000" in HEADER, "stable uptime threshold changed")

    for token in (
        "esp_ota_get_running_partition",
        "esp_ota_get_boot_partition",
        "esp_ota_get_state_partition",
        "esp_partition_verify",
        "ESP_OTA_IMG_PENDING_VERIFY",
        "ESP_OTA_IMG_VALID",
        "ELIGIBLE_TO_CONFIRM",
        "ALREADY_VALID",
        "startup_complete",
        "core_services_ready",
        "config_ready",
        "display_ready",
        "fatal_fault_free",
        "stable_uptime_ms",
    ):
        need(token in COMBINED, f"missing rollback health token: {token}")

    # Network availability must never be required to validate a locally healthy boot.
    for forbidden_field in ("wifi_ready", "network_ready", "internet_ready",
                            "api_connected", "cloud_ready"):
        need(forbidden_field not in COMBINED.lower(),
             f"network-dependent health evidence leaked in: {forbidden_field}")

    state_pos = SOURCE.find("esp_ota_get_state_partition")
    valid_pos = SOURCE.find("ESP_OTA_IMG_VALID", state_pos)
    pending_pos = SOURCE.find("ESP_OTA_IMG_PENDING_VERIFY", valid_pos)
    health_pos = SOURCE.find("evidence.startup_complete", pending_pos)
    uptime_pos = SOURCE.find("evidence.stable_uptime_ms", health_pos)
    need(state_pos >= 0 and valid_pos > state_pos and pending_pos > valid_pos,
         "OTA state decision ordering is wrong")
    need(health_pos > pending_pos and uptime_pos > health_pos,
         "local health checks must follow PENDING_VERIFY state check")

    forbidden = (
        "esp_ota_mark_app_valid_cancel_rollback",
        "esp_ota_mark_app_invalid_rollback",
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
    need(not leaked, f"read-only rollback policy contains mutation APIs: {leaked}")

    for token in ("ota_rollback_health_policy", "OtaRollbackHealthPolicy"):
        need(token not in GUARD, f"rollback health policy wired into runtime guard: {token}")
    need("usp_ota_guard" not in PUBLIC and "OtaRollbackHealthPolicy" not in PUBLIC,
         "private rollback health policy leaked into public first-flash")

    global_instance = re.search(
        r"(?m)^\s*(?:static\s+)?OtaRollbackHealthPolicy\s+[A-Za-z_]\w*\s*[;{(]",
        SOURCE,
    )
    need(global_instance is None, "global/static rollback health policy instance exists")

    print(
        "OTA_ROLLBACK_HEALTH_POLICY_STATIC=PASS read_only=1 pending_verify=1 "
        "already_valid=1 uptime_ms=30000 local_health=5 network_independent=1 "
        "unwired=1 no_mark_valid=1 no_reboot=1 public_isolated=1"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_ROLLBACK_HEALTH_POLICY_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
