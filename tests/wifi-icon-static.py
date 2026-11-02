#!/usr/bin/env python3
"""Guard the root-level Home Wi-Fi badge and cached RSSI binding."""

import argparse
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def section(text, start_marker, end_marker):
    """Return one bounded source section."""
    start = text.index(start_marker)
    end = text.index(end_marker, start + len(start_marker))
    return text[start:end]


def root_widget(text, widget_type, widget_id):
    """Return one top-level Home widget block by type and identifier."""
    marker = f"          - {widget_type}:\n              id: {widget_id}\n"
    start = text.index(marker)
    end = text.find("\n          - ", start + len(marker))
    return text[start : end if end >= 0 else len(text)]


def wifi_visual_block(text):
    """Return the complete root-level Wi-Fi visual group."""
    start = text.index("          - label:\n              id: dashboard_wifi_badge\n")
    end = text.index(
        "          - label:\n              id: dashboard_header_mains_caption\n",
        start,
    )
    return text[start:end]


def check_badge(text):
    """Require a plain-text root-level marker with a visible background."""
    block = root_widget(text, "label", "dashboard_wifi_badge")
    tokens = (
        "x: 660", "y: 8", "width: 66", "height: 44", "radius: 8",
        "bg_color: 0x122B46", "bg_opa: 100%", "border_width: 1",
        "border_color: 0x263D57", 'text: "WiFi"',
        "text_font: usp_i18n_18", "text_color: 0x38BDF8",
    )
    for token in tokens:
        assert token in block, token
    assert "widgets:" not in block


def check_bars(text):
    """Require four independent root-level signal bars inside badge bounds."""
    geometry = (
        ("dashboard_wifi_bar_1", 699, 36, 8),
        ("dashboard_wifi_bar_2", 706, 32, 12),
        ("dashboard_wifi_bar_3", 713, 28, 16),
        ("dashboard_wifi_bar_4", 720, 24, 20),
    )
    for name, x, y, height in geometry:
        block = root_widget(text, "obj", name)
        tokens = (
            f"x: {x}", f"y: {y}", "width: 4", f"height: {height}",
            "radius: 1", "bg_opa: 100%", "border_width: 0",
            "scrollable: false",
        )
        for token in tokens:
            assert token in block, (name, token)
        assert "widgets:" not in block


def check_refresh(text):
    """Require cached RSSI and deterministic foreground visibility repair."""
    script = section(text, "  - id: refresh_wifi_icon\n", "\n  - id:")
    assert "wifi->is_connected()" in script
    assert "wifi->wifi_rssi()" not in script
    assert "id(panel_wifi_rssi_valid)" in script
    assert "id(panel_wifi_rssi).state" in script
    assert "usp_wifi_icon::controller().update(" in script
    assert 'label_text(id(dashboard_wifi_badge), "WiFi")' in script
    assert "lv_obj_clear_flag(id(dashboard_wifi_badge)" in script
    assert "lv_obj_move_foreground(id(dashboard_wifi_badge))" in script
    for index in range(1, 5):
        assert f"id(dashboard_wifi_bar_{index})" in script
    assert script.index("move_foreground(id(dashboard_wifi_badge))") < script.index(
        "lv_obj_move_foreground(bar)"
    )
    assert "home_native_frame_data_generation" in script
    assert "dashboard_wifi_label" in script


def check_sources(text):
    """Require one cached RSSI source and independent Web projection."""
    sensors = text[text.index("\nsensor:\n") :]
    panel = sensors[sensors.index("id: panel_wifi_rssi") - 30 :]
    panel = panel[: panel.index("\n  - platform:", 40)]
    assert "platform: wifi_signal" in panel
    assert "internal: true" in panel and "update_interval: 15s" in panel
    assert "script.execute: refresh_wifi_icon" in panel
    assert "id(panel_wifi_rssi_valid) = std::isfinite(x)" in panel
    web = sensors[sensors.index("id: web_wifi_rssi") - 30 :]
    web = web[: web.index("\n  - platform:", 40)]
    assert "platform: template" in web
    assert "id(panel_wifi_rssi).state" in web


def check_events(text):
    """Require boot, connect, disconnect, tick and locale refresh paths."""
    wifi = section(text, "\nwifi:\n", "\ncaptive_portal:")
    assert wifi.count("script.execute: refresh_wifi_icon") == 2
    assert "id(panel_wifi_rssi_valid) = false" in wifi
    tick = section(text, "  - interval: 1s", "  - interval: 5min")
    assert "script.execute: refresh_wifi_icon" in tick
    localize = section(text, "  - id: localize_page_home", "\n  - id:")
    assert localize.count("script.execute: refresh_wifi_icon") == 3


def check_rendering(helper, text):
    """Forbid old physical-failure paths and retain ordinary object fills."""
    assert "lv_obj_set_style_bg_color(" in helper
    assert "lv_obj_set_style_bg_opa(" in helper
    assert "LV_EVENT_DRAW_POST" not in helper
    assert "lv_obj_add_event_cb(" not in helper
    assert "lv_draw_arc(" not in helper and "lv_draw_line(" not in helper
    assert "lv_obj_set_style_image_recolor" not in helper
    images = section(text, "\nimage:\n", "\nlvgl:")
    assert "dashboard_wifi_icon_mask" not in images
    assert "wifi-icon-mask.png" not in images


def check_profiles(text):
    """Require private/public profiles to retain the exact Wi-Fi visual group."""
    private = (ROOT / "firmware/ukraine-status-panel-production.yaml").read_text()
    public = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
    expected = wifi_visual_block(text)
    assert wifi_visual_block(private) == expected
    assert wifi_visual_block(public) == expected
    assert "id: panel_wifi_rssi" in public
    assert "id: web_wifi_rssi" not in public
    assert "wifi->wifi_rssi()" not in public


def main():
    """Validate the Home Wi-Fi indicator without network or panel access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    text = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    helper = (ROOT / "include/usp_wifi_icon.h").read_text()
    check_badge(text)
    check_bars(text)
    check_refresh(text)
    check_sources(text)
    check_events(text)
    check_rendering(helper, text)
    check_profiles(text)
    logger.info(
        "WIFI_ICON_STATIC=PASS root_badge root_bars foreground cached_rssi"
    )


if __name__ == "__main__":
    main()
