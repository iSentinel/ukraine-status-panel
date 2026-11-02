#!/usr/bin/env python3
"""Check the battery support wiring without contacting hardware."""

import argparse
from pathlib import Path

from loguru import logger

from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware/ukraine-status-panel.yaml"


def main():
    """Require manual presence, model-based level, UI, and no fake charger state."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = CONFIG.read_text()
    version = canonical_firmware_version()
    assert f'firmware_version: "{version}"' in source
    required = (
        '- include/usp_battery_model.h',
        'id: web_battery_installed',
        'name: "Battery Installed"',
        'restore_mode: RESTORE_DEFAULT_OFF',
        'id: panel_battery_level',
        'name: "Panel Battery Level"',
        'id: web_battery_status',
        'name: "Panel Battery Status"',
        'id: battery_installed_button',
        'id: settings_battery_status_label',
        'usp_battery_model::estimate_percent',
        'usp_battery_model::state(',
    )
    missing = [token for token in required if token not in source]
    assert not missing, f"missing battery support tokens: {missing}"
    battery_block = source[source.index('id: web_battery_installed'):]
    battery_block = battery_block[:battery_block.index('\n  - platform:', 20)]
    assert "Charging" not in battery_block
    assert "charger" not in battery_block.lower()
    logger.info(
        "BATTERY_SUPPORT_STATIC=PASS manual_presence=1 fake_charging=0 raw_adc_reused=1"
    )


if __name__ == "__main__":
    main()
