#!/usr/bin/env python3
"""Validate Web/shared-setting round-trip and persistence QA plumbing."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / 'firmware/ukraine-status-panel.yaml').read_text(encoding='utf-8')
LIVE = (ROOT / 'tests/web-config-persistence-live-probe.py').read_text(encoding='utf-8')
RUNNER = (ROOT / 'scripts/run-regression.sh').read_text(encoding='utf-8')
REGION = (ROOT / 'components/usp_region/region_settings.cpp').read_text(encoding='utf-8')
OUTAGE = (ROOT / 'components/usp_outage/outage_client.cpp').read_text(encoding='utf-8')


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, 'wrong production version')
    for block in (
        'id: ui_language\n    type: uint8_t\n    restore_value: yes',
        'id: ui_sound_enabled\n    type: bool\n    restore_value: yes',
        'id: ui_alert_palette\n    type: uint8_t\n    restore_value: yes',
    ):
        need(block in PROD, f'persisted global missing: {block.splitlines()[0]}')
    for token in (
        'name: "UI Language"', 'id: set_ui_language',
        'name: "Alert Palette"', 'id: set_alert_palette',
        'name: "Selected Region"', 'id(set_selected_region).execute(',
        'name: "Outage Group"', 'id: set_outage_group',
        'name: "Restart"', 'action: usp_qa_dump_shared_settings_134',
    ):
        need(token in PROD, f'round-trip backend missing: {token}')
    need('save_selected_region_preference_' in REGION and 'selected_region_pref_.save' in REGION,
         'selected-region persistence queue path missing')
    need('group_pref_.save' in OUTAGE, 'outage-group persistence queue path missing')
    need('global_preferences->sync()' not in REGION,
         'selected-region interactive path must not force global sync')
    need('global_preferences->sync()' not in OUTAGE,
         'outage-group interactive path must not force global sync')
    for token in (
        'USP_ALLOW_PERSISTENCE_MUTATION', 'USP_ALLOW_REBOOT',
        'verify_device(client, CONFIG)', 'web_set(web,',
        "'UI Language'", "'Alert Palette'", "'Selected Region'", "'Outage Group'",
        'button_command', 'PERSISTENCE_AFTER_REBOOT=PASS',
        'PERSISTENCE_RESTORE_AFTER_REBOOT=PASS',
    ):
        need(token in LIVE, f'live persistence guard/step missing: {token}')
    need('run web-config-roundtrip-static.py' in RUNNER, 'static round-trip test not in regression')
    need('run_live web-config-persistence-live-probe.py' in RUNNER, 'live persistence probe not gated')
    print('WEB_CONFIG_ROUNDTRIP_STATIC=PASS')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'WEB_CONFIG_ROUNDTRIP_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
