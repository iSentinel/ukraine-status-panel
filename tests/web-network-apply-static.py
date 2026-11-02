#!/usr/bin/env python3
"""Guard explicit Wi-Fi apply transaction against boot/network races."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        "web_network_apply_in_progress", "web_network_apply_failed",
        "web_network_reconnect_required", "credentials_changed",
        "if (!credentials_changed) return;", "wifi->save_wifi_sta(ssid, password)",
        "wifi->restart_adapter();", "timeout: 30s",
        "wifi != nullptr && wifi->is_connected()",
        "Wi-Fi reconnect timed out; IP profile not applied",
        "IP profile deferred: Wi-Fi is not connected",
        "esp_netif_dhcpc_get_status",
        "IPv4 profile unchanged; DHCP already active",
        "IPv4 profile unchanged; static profile already active",
        "esp_netif_get_ip_info", "esp_netif_get_dns_info",
        'return {"APPLYING"}', 'std::string("RECONNECT FAILED")',
    ):
        need(token in PROD, f"network apply transaction missing: {token}")
    boot = PROD.split("# WEB_RUNTIME_BEGIN: integrations-boot", 1)[1].split(
        "# WEB_RUNTIME_END: integrations-boot", 1)[0]
    need("apply_network_profile" not in boot, "credential apply must never run from on_boot")
    wifi_block = PROD.split("wifi:", 1)[1].split("captive_portal:", 1)[0]
    need("restore_static_network_profile" in wifi_block,
         "static IP restore is not wired to Wi-Fi on_connect")
    need("apply_network_profile" not in wifi_block,
         "credential apply must never run from Wi-Fi on_connect")
    restore = PROD.split("- id: restore_static_network_profile", 1)[1].split(
        "- id: apply_network_profile", 1)[0]
    need('current_option() == "Static"' in restore and "delay: 1500ms" in restore and
         "script.execute: apply_network_ip_profile" in restore,
         "static IP restore must be delayed, Static-only, and IP-only")
    apply = PROD.split("- id: apply_network_profile", 1)[1].split(
        "- id: apply_network_ip_profile", 1)[0]
    need("delay: 2500ms" not in apply, "fixed reconnect delay returned")
    need("wifi->disable()" not in apply and "wifi->enable()" not in apply,
         "unsafe adapter disable/enable returned")
    need("staged_ssid != current.get_ssid()" in apply and
         "sta.set_password(staged_password)" in apply,
         "new SSID must not inherit previous network password")
    password = PROD.split("id: web_network_password", 1)[1].split(
        "- platform: template", 1)[0]
    need("restore_value: false" in password,
         "staged Wi-Fi password must not persist as template entity state")
    need('id(web_network_password).clear_value()' in apply,
         "staged Wi-Fi password must be cleared after Apply")
    for token in ("web_network_apply_in_progress", "apply_network_profile",
                  "Network Profile Status"):
        need(token not in PUBLIC, f"public first-flash leaked private network runtime: {token}")
    print("WEB_NETWORK_APPLY_STATIC=PASS explicit_only=1 wait_connected=1 timeout=30s no_boot_race=1 no_old_password_carry=1 static_restore=ip_only password=transient ip_profile_idempotent=1")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_NETWORK_APPLY_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
