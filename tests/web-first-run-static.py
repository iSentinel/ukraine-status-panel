#!/usr/bin/env python3
"""Validate guided first-run Web setup without duplicated mutation paths."""
from pathlib import Path
from web_ui_static_source import read_source
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
JS = read_source(ROOT / "firmware/webui/usp-web-ui.src.js")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        'id: web_setup_complete', 'id: web_group_setup', 'name: "Setup"',
        'id: web_setup_status', 'id: web_setup_finish_button',
        'id(web_setup_complete) = true', 'timezone: Europe/Kyiv',
    ):
        need(token in PROD, f"production missing: {token}")
    need('id: web_setup_complete\n    type: bool\n    restore_value: yes' in PROD,
         "setup completion is not persisted")
    for token in (
        '"setup"', '["Setup","setup"]', 'setupPanel(t)', 'Europe/Kyiv',
        '["Wi-Fi SSID","network"]', '["UI Language","overview"]',
        '["Selected Region","location"]', '["Weather Location","location"]',
        '["Outage Group Code","svitlo"]',
    ):
        need(token in JS, f"Web setup missing: {token}")
    need('id(ui_selected_region_id) =' not in JS,
         "setup UI must not write region globals directly")
    need('id(outage_client).set_group(' not in JS,
         "setup UI must not bypass outage setter")
    need(PUBLIC.exists(), "public YAML missing")
    public = PUBLIC.read_text()
    for token in ('web_setup_complete', 'web_group_setup', 'web_setup_status', 'Finish Setup', 'Repeat Setup'):
        need(token not in public, f"public profile leaked setup runtime: {token}")
    print("WEB_FIRST_RUN_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_FIRST_RUN_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
