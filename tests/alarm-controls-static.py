#!/usr/bin/env python3
"""Guard alarm controls, honest time handling and independent alarm audio."""
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_model():
    """Keep persistence and audible notification in the correct order."""
    cpp = (ROOT / "components/usp_alarm/alarm_manager.cpp").read_text()
    schedule = (ROOT / "components/usp_alarm/alarm_schedule.h").read_text()
    header = (ROOT / "components/usp_alarm/alarm_manager.h").read_text()
    start = cpp.index("const uint8_t due =")
    body = cpp[start:cpp.index("}  // namespace", start)]
    assert body.index("persist_(") < body.index("alarm_trigger_.trigger")
    assert "uint8_t enabled{0}" in schedule
    assert "sizeof(Store) == 32" in schedule
    assert "firing_guard_active(record.fired_day, clock.local)" in schedule
    assert "if (delta != 1) return 0;" in schedule
    assert "rtttl" not in cpp and "http" not in cpp.lower()
    assert "revision != revisions_[index]" in cpp
    assert "if (!storage_ok_) return;" not in cpp
    assert "queue_firing_guard_retry_();" in cpp
    assert "STORAGE_RETRY_MS = 30000" in header


def check_controls():
    """Require bounded UI, one settings path and independent personal sound."""
    yaml = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    path = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
    public = path.read_text()
    assert "id: page_alarms" in yaml and "page: 11" in yaml
    for index in range(3):
        for kind in ("time", "repeat", "enabled"):
            assert f"id: web_alarm_{kind}_{index}" in yaml
    assert "const auto &manager = *id(personal_alarms);" in yaml
    assert "const auto &manager = id(personal_alarms);" not in yaml
    assert "- lambda: id(personal_alarms).dismiss();" in yaml
    assert "static_cast<int>((entry.x1 + entry.x2) / 2)" in yaml
    assert "id(personal_alarms).configure" in yaml
    assert "id(alarm_form_revisions)[slot]" in yaml
    alarm_on = yaml.split("  on_alarm:", 1)[1].split("\n\nusp_room_history:", 1)[0]
    assert "ui_sound_enabled" not in alarm_on
    assert "script.execute: alarm_audio_loop" in alarm_on
    assert "id: alarm_sound_enabled" in yaml and 'initial_value: "true"' in yaml
    assert "id: web_alarm_sound_enabled" in yaml
    assert "id: alarms_sound_toggle" in yaml
    assert "id: set_alarm_sound_enabled" in yaml
    loop = yaml.split("  - id: alarm_audio_loop", 1)[1].split("  - id: play_buzzer_test", 1)[0]
    assert "id(personal_alarms).active() && id(alarm_sound_enabled)" in loop
    assert 'rtttl: "alarm:d=16,o=6,b=120:c,8p,c"' in loop
    assert "delay: 6s" in loop
    general = yaml.split("  - id: set_sound_enabled", 1)[1].split("  - id: set_alarm_sound_enabled", 1)[0]
    assert "rtttl.stop" not in general and "alarm_audio_loop" not in general
    assert "id(alarm_notification_sound), id(alarm_sound_enabled) ?" in yaml
    assert "id: personal_alarms" in public and "web_alarm_" not in public
    assert "timezone: Europe/Kyiv" in yaml
    assert "MAIN_PAGES[] = {0, 1, 2, 3, 4, 12, 9}" in (
        ROOT / "include/usp_swipe_navigation.h").read_text()


def check_presentation():
    """Keep user-visible day choices and the Clock entry unambiguous."""
    yaml = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "id: clock_alarms" in yaml
    route = yaml.split("  - id: navigate_lcd_page", 1)[1].split(
        "  # M2 production fast-first-paint", 1)[0]
    assert "return page == 11;" in route and "id: page_alarms" in route
    assert "id: settings_alarms" not in yaml
    assert "id: alarm_form_repeat" in yaml
    assert "usp_alarm_ui::mask(id(alarm_form_repeat)[slot])" in yaml
    for slot in range(3):
        for preset in range(3):
            assert f"id: alarm_repeat_{slot}_{preset}" in yaml
        assert f"id: alarm_days_{slot}" in yaml
    assert "МАПА / НА ВЕСЬ ЕКРАН" not in yaml
    assert "КАРТА / НА ВЕСЬ ЭКРАН" not in yaml
    assert 'text: "MAP / FULLSCREEN"' not in yaml
    title = yaml.split("id: fullscreen_map_title_label", 1)[1].split(
        "- label:", 1)[0]
    assert "width: 250" in title and "long_mode: DOT" in title


def main():
    """Run without accessing device time, flash or the buzzer."""
    check_model()
    check_controls()
    check_presentation()
    logger.info(
        "ALARM_STATIC=PASS three_slots independent_alarm_audio repeat6000"
    )


if __name__ == "__main__":
    main()
