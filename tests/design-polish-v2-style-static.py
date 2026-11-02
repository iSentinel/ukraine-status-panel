#!/usr/bin/env python3
"""Guard Design Polish v2 cross-screen LCD style contracts."""

import argparse
import importlib.util
from pathlib import Path

from loguru import logger
from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    "ukraine-status-panel.yaml",
    "ukraine-status-panel-production.yaml",
    "ukraine-status-panel-public-first-flash.yaml",
)
SURFACE = {
    "radius": 16,
    "bg_color": 0x0C1828,
    "border_width": 1,
    "border_color": 0x263D57,
    "scrollable": False,
}


def load_config(path):
    """Load project YAML without resolving ESPHome private tags."""
    source = ROOT / "tests/lcd-navigation-static.py"
    spec = importlib.util.spec_from_file_location("nav_loader", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ReferenceLoader.add_multi_constructor("!", module.unknown_tag)
    return module.yaml.load(path.read_text(), Loader=module.ReferenceLoader)


def walk(items):
    """Yield nested LVGL widget payloads."""
    for item in items:
        for payload in item.values():
            if not isinstance(payload, dict):
                continue
            yield payload
            yield from walk(payload.get("widgets", []))


def pages(config):
    """Return LVGL pages indexed by id."""
    entries = config["lvgl"][0]["pages"]
    return {entry["id"]: entry for entry in entries}


def widgets(config):
    """Return placed widgets indexed by id."""
    result = {}
    for current in pages(config).values():
        for item in walk(current.get("widgets", [])):
            widget_id = item.get("id")
            if widget_id:
                result[widget_id] = item
    return result


def check_surface(item, label):
    """Require the common primary panel surface."""
    for key, value in SURFACE.items():
        assert item.get(key) == value, f"{label}: {key} drift"


def check_titles(items):
    """Require one hierarchy for screen and Home card titles."""
    page_ids = (
        "weather_title_label",
        "fullscreen_map_title_label",
        "i18n_038",
        "i18n_051",
        "room_title",
        "spotify_title",
    )
    for widget_id in page_ids:
        item = items[widget_id]
        assert item["text_font"] == "usp_i18n_28", widget_id
        assert item["text_color"] == 0xF8FAFC, widget_id
    card_ids = (
        "dashboard_map_title_label",
        "dashboard_alerts_title_label",
        "dashboard_power_title_label",
        "dashboard_history_title_label",
        "dashboard_outage_title_label",
    )
    for widget_id in card_ids:
        item = items[widget_id]
        assert item["text_font"] == "usp_i18n_20", widget_id
        assert item["text_color"] == 0xF8FAFC, widget_id


def check_home(items):
    """Check the five Home surfaces and compact semantic badges."""
    cards = (
        "dashboard_map_card",
        "dashboard_alerts_card",
        "dashboard_power_card",
        "dashboard_history_card",
        "dashboard_outage_card",
    )
    for card_id in cards:
        check_surface(items[card_id], card_id)
    for badge in ("dashboard_live_badge", "dashboard_alert_count_label"):
        assert items[badge]["radius"] == 8, badge


def top_objects(config, page_id):
    """Return top-level object payloads on one page."""
    current = pages(config)[page_id]
    return [entry["obj"] for entry in current["widgets"] if "obj" in entry]


def check_panels(config, items):
    """Check primary surfaces across Weather, Power, Room and Spotify."""
    groups = (("page_weather", 6), ("page_power", 3), ("page_room", 5))
    for page_id, expected in groups:
        objects = top_objects(config, page_id)
        assert len(objects) >= expected, page_id
        for index, item in enumerate(objects[:expected]):
            check_surface(item, f"{page_id}[{index}]")
    for card_id in ("spotify_art_card", "spotify_player_card"):
        check_surface(items[card_id], card_id)


def check_controls(items):
    """Require the shared 10 px control radius where controls exist."""
    control_ids = (
        "dashboard_map_expand_button",
        "weather_refresh_button",
        "weather_fullscreen_button",
        "weather_back_button",
        "weather_room_button",
        "room_period_24h_button",
        "room_period_7d_button",
        "room_period_30d_button",
        "room_period_1y_button",
        "room_weather_button",
        "room_home_button",
        "spotify_queue_button",
        "spotify_modes_button",
        "spotify_prev",
        "spotify_play",
        "spotify_next",
        "spotify_device",
    )
    for widget_id in control_ids:
        if widget_id in items:
            assert items[widget_id]["radius"] == 10, widget_id


def verify(path):
    """Verify one source or generated firmware profile."""
    config = load_config(path)
    assert config["substitutions"]["firmware_version"] == canonical_firmware_version()
    items = widgets(config)
    check_titles(items)
    check_home(items)
    check_panels(config, items)
    check_controls(items)


def main():
    """Validate all profiles without panel or network access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    for name in PROFILES:
        verify(ROOT / "firmware" / name)
    logger.info(
        "DESIGN_POLISH_V2_STYLE=PASS profiles=3 screens=6 "
        "panel=shared titles=shared controls=10 badges=8"
    )


if __name__ == "__main__":
    main()
