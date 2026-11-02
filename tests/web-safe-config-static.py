#!/usr/bin/env python3
"""Validate safe browser-only config backup and Repeat Setup contracts."""
from pathlib import Path
from web_ui_static_source import read_source
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
JS = read_source(ROOT / "firmware/webui/usp-web-ui.src.js")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        'id: web_setup_repeat_button', 'name: "Repeat Setup"',
        'id(web_setup_complete) = false', 'sorting_group_id: web_group_maintenance',
    ):
        need(token in PROD, f"Repeat Setup backend missing: {token}")
    for token in (
        'safeBackup() {', 'schema:"usp-safe-config-v1"',
        'JSON.stringify(doc,null,2)', 'URL.createObjectURL(blob)',
        'ukraine-status-panel-safe-', 'maintenancePanel(t)',
        'entity.name === "Repeat Setup"',
    ):
        need(token in JS, f"safe backup UI missing: {token}")
    method = JS.split('safeBackup() {', 1)[1].split('maintenancePanel(t) {', 1)[0]
    for token in (
        'Network Password', 'Network SSID', 'New Web Password',
        'SvitloBot Channel Key', 'api_encryption_key', 'ota_password',
    ):
        need(token not in method, f"secret/sensitive field leaked into backup allowlist: {token}")
    for token in (
        'UI Language', 'Alert Palette', 'Selected Region', 'Weather Location',
        'Outage Group Code', 'Panel Sounds Enabled', 'Network IPv4 Mode',
        'Network Static IP', 'Starlink Host', 'SvitloBot Enabled', 'Europe/Kyiv',
    ):
        need(token in method, f"safe backup allowlist missing: {token}")
    for token in ('web_setup_repeat_button', 'Repeat Setup', 'usp-safe-config-v1'):
        need(token not in PUBLIC, f"public bootstrap leaked production backup/setup runtime: {token}")
    print("WEB_SAFE_CONFIG_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_SAFE_CONFIG_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
