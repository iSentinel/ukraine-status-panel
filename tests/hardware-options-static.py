#!/usr/bin/env python3
"""Validate the solderless optional hardware contract."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()
COMPONENT = ROOT / "components/usp_optional_sht4x"


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def check_sound_paths():
    """Allow only the repeating personal alarm cadence and explicit user test."""
    alarm = PROD[PROD.index("usp_alarm:\n"):PROD.index("usp_room_history:\n")]
    loop = PROD[PROD.index("  - id: alarm_audio_loop"):
                PROD.index("  - id: play_buzzer_test")]
    test = PROD[PROD.index("  - id: play_buzzer_test"):
                PROD.index("  - id: refresh_sound_controls")]
    require("on_alarm:" in alarm and "script.execute: alarm_audio_loop" in alarm,
            "committed alarm must start the bounded audio loop")
    require("rtttl.play:" not in alarm, "on_alarm must not own a one-shot tone")
    require(loop.count("rtttl.play:") == 1, "personal alarm cadence tone missing")
    require("id(personal_alarms).active() && id(alarm_sound_enabled)" in loop,
            "alarm cadence must require active alarm and independent sound policy")
    require("delay: 6s" in loop, "alarm cadence must remain bounded at 6 seconds")
    require(test.count("rtttl.play:") == 1, "explicit test tone missing")
    remaining = PROD.replace(loop, "", 1).replace(test, "", 1)
    require("rtttl.play:" not in remaining, "unexpected additional sound path")
    schedule = (ROOT / "components/usp_alarm/alarm_schedule.h").read_text()
    require("uint8_t enabled{0}" in schedule, "alarms must default disabled")


def main() -> int:
    require(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        "- usp_optional_sht4x",
        "- platform: usp_optional_sht4x",
        "id: environment_sht4x",
        "i2c_id: bus_a",
        "address: 0x44",
        'name: "Environment Temperature"',
        'name: "Environment Humidity"',
        "- platform: ledc",
        "id: buzzer_pwm",
        "pin: GPIO6",
        "id: panel_buzzer",
        'name: "Buzzer Test"',
        "sorting_group_id: web_group_hardware",
        'name: "Panel Sounds Enabled"',
        'name: "Alarm Sound Enabled"',
        "id: settings_sound_toggle_button",
        "id: alarms_sound_toggle",
        "id: alarm_sound_enabled",
        "id: set_alarm_sound_enabled",
        "id: alarm_audio_loop",
        "id: settings_buzzer_test_button",
        "id: diag_sht4x_value_label",
        "id: set_sound_enabled",
        "id: refresh_sound_controls",
        "id: refresh_optional_hardware_ui",
        "action: usp_qa_set_sound_143",
        "action: usp_qa_buzzer_test_143",
        "action: usp_qa_dump_optional_hardware_143",
        'initial_value: "false"',
    ):
        require(token in PROD, f"hardware contract missing: {token}")

    for filename in ("sensor.py", "optional_sht4x.h", "optional_sht4x.cpp"):
        require((COMPONENT / filename).exists(), f"optional SHT4x file missing: {filename}")
    source = (COMPONENT / "optional_sht4x.cpp").read_text()
    require("mark_failed" not in source, "optional SHT4x must not fail setup when absent")
    require("get_8bit_register" not in source,
            "optional SHT4x presence probe must not emit Sensirion ERROR logs")
    for token in (
        "this->write(&command, 1)",
        "i2c::ERROR_NOT_ACKNOWLEDGED",
        "this->read(raw, sizeof(raw))",
        "sensirion_common::CRC_POLYNOMIAL",
        "SHT4x probe I2C fault",
        "SHT4x probe CRC fault",
        "ever_detected_",
        "SHT4x disconnected",
    ):
        require(token in source, f"quiet SHT4x probe contract missing: {token}")
    for token in (
        "MEASURE_HIGH_PRECISION = 0xFD",
        "Optional SHT4x not detected",
        "Optional SHT4x detected",
        "publish_unavailable_",
    ):
        require(token in source, f"optional SHT4x behavior missing: {token}")

    for token in (
        'const TAB_ORDER = ["setup", "overview", "location", "hardware", "svitlo", "network", "shelly", "starlink", "spotify", "maintenance"]',
        '["Hardware","hardware"]',
        'hardware:"SHT41/SHT45 (I²C 0x44) и буззер',
        '["SHT4x Status","Стан SHT41/SHT45","Состояние SHT41/SHT45"]',
    ):
        require(token in JS, f"Hardware Web UI contract missing: {token}")

    check_sound_paths()
    require('id: play_buzzer_test' in PROD, "explicit buzzer-test script missing")
    require('id: ui_sound_enabled' in PROD and 'initial_value: "false"' in PROD,
            "UI sound policy must remain default off")
    alarm_global = PROD[PROD.index("  - id: alarm_sound_enabled"):
                        PROD.index("  # 0.115.114 rendering-only alert palette")]
    require('initial_value: "true"' in alarm_global,
            "personal alarm sound must default on independently")

    for token in (
        "- usp_optional_sht4x",
        "- platform: usp_optional_sht4x",
        "pin: GPIO6",
        "id: panel_buzzer",
        "id: ui_sound_enabled",
        "id: alarm_sound_enabled",
        "id: settings_sound_toggle_button",
        "id: alarms_sound_toggle",
        "id: set_alarm_sound_enabled",
        "id: alarm_audio_loop",
        "id: settings_buzzer_test_button",
        "id: diag_sht4x_value_label",
        "id: set_sound_enabled",
        "id: play_buzzer_test",
    ):
        require(token in PUBLIC, f"public bootstrap lost base hardware support: {token}")
    for token in (
        "web_group_hardware",
        'name: "Buzzer Test"',
        'name: "SHT4x Status"',
        'name: "Panel Sounds Enabled"',
        'name: "Alarm Sound Enabled"',
        "id: web_panel_sounds_enabled",
        "id: web_alarm_sound_enabled",
    ):
        require(token not in PUBLIC, f"public bootstrap leaked Web-only hardware UI: {token}")

    print("HARDWARE_OPTIONS_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"HARDWARE_OPTIONS_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
