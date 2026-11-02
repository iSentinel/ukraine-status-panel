#!/usr/bin/env python3
"""Guard the read-only local Web status/diagnostics surface."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"WEB_STATUS_DIAGNOSTICS_STATIC=FAIL {message}")


require("id: web_group_status" in PROD, "missing Web status group")
require('name: "Status & diagnostics"' in PROD, "missing status group name")
require(
    'name: "Status & diagnostics"\n      sorting_weight: -80' in PROD,
    "wrong status group priority",
)

for entity in (
    "web_firmware_version",
    "web_panel_ip",
    "web_wifi_ssid",
    "web_wifi_rssi",
    "web_uptime_seconds",
    "web_heap_free",
    "web_psram_free",
    "web_ota_trust_key",
    "web_ota_update_policy",
    "web_ota_device_verifier",
):
    require(f"id: {entity}" in PROD, f"missing {entity}")

for existing in (
    "panel_loop_stack_headroom",
    "alert_data_age_seconds",
    "alert_accepted_frames_sensor",
    "alert_rejected_frames_sensor",
    "alert_map_source_text",
    "alert_live_state_text",
    "alert_live_revision_text",
):
    start = PROD.index(f"id: {existing}")
    fragment = PROD[start:start + 900]
    require(
        "sorting_group_id: web_group_status" in fragment,
        f"{existing} not grouped",
    )

for forbidden in (
    "web_group_status",
    "web_firmware_version",
    "web_panel_ip",
    "web_wifi_ssid",
    "web_wifi_rssi",
    "web_uptime_seconds",
    "web_heap_free",
    "web_psram_free",
    "web_ota_trust_key",
    "web_ota_update_policy",
    "web_ota_device_verifier",
):
    require(forbidden not in PUBLIC, f"public bootstrap leaked {forbidden}")

print("WEB_STATUS_DIAGNOSTICS_STATIC=PASS")
