#!/usr/bin/env python3
"""Guard Home 2.0 primary-state hierarchy across firmware profiles."""

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
CAPTIONS = (
    "dashboard_mains_caption_label",
    "dashboard_battery_caption_label",
    "dashboard_wifi_caption_label",
    "dashboard_yasno_caption_label",
)


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
    for entry in items:
        for payload in entry.values():
            if not isinstance(payload, dict):
                continue
            yield payload
            yield from walk(payload.get("widgets", []))


def widgets(config):
    """Return all placed widgets indexed by stable id."""
    result = {}
    for page in config["lvgl"][0]["pages"]:
        for payload in walk(page.get("widgets", [])):
            widget_id = payload.get("id")
            if widget_id:
                result[widget_id] = payload
    return result


def check_power(items):
    """Require four visible decision states and no duplicate Yasno row."""
    expected = {
        "home_grid_status_label": (16, 48, 174, 36, "usp_i18n_20"),
        "dashboard_shelly_summary_label": (204, 48, 174, 36, "usp_i18n_20"),
        "dashboard_battery_label": (16, 96, 174, 34, "usp_i18n_18"),
        "dashboard_wifi_label": (204, 96, 174, 34, "usp_i18n_18"),
    }
    for widget_id, geometry in expected.items():
        item = items[widget_id]
        actual = (
            item["x"], item["y"], item["width"], item["height"],
            item["text_font"],
        )
        assert actual == geometry, (widget_id, actual)
        assert not item.get("hidden", False), widget_id
    for widget_id in CAPTIONS + ("dashboard_group_label",):
        assert items[widget_id].get("hidden") is True, widget_id


def check_environment(items):
    """Require Weather/Room overview and hide the obsolete third row."""
    first = items["dashboard_history_0"]
    second = items["dashboard_history_1"]
    third = items["dashboard_history_2"]
    assert (first["y"], first["height"], first["text_font"]) == (
        42, 30, "usp_i18n_20"
    )
    assert (second["y"], second["height"], second["text_font"]) == (
        78, 30, "usp_i18n_20"
    )
    assert third.get("hidden") is True


def check_alert(items):
    """Require the alert count to read as a primary state."""
    item = items["dashboard_alert_count_label"]
    assert (item["x"], item["y"], item["width"], item["height"]) == (
        296, 6, 80, 36
    )
    assert item["text_font"] == "usp_i18n_20"
    assert item["radius"] == 8


def script_block(text, script_id, next_id):
    """Return one exact YAML script block."""
    start = text.index(f"  - id: {script_id}\n")
    end = text.index(f"\n  - id: {next_id}\n", start)
    return text[start:end]


def check_delivery(path):
    """Require existing snapshots to feed Home without new provider calls."""
    text = path.read_text()
    history = script_block(
        text, "refresh_dashboard_history_ui", "refresh_dashboard_ui"
    )
    assert "weather_client).current_snapshot()" in history
    assert "environment_sht4x).detected()" in history
    assert "history_store).event_summary" not in history
    shelly = script_block(text, "refresh_shelly_power_ui", "localize_page_power")
    assert "dashboard_shelly_summary_label" in shelly
    assert "EM1.GetStatus" not in shelly
    assert "Shelly.GetDeviceInfo" not in shelly


def verify(path):
    """Verify one source or generated firmware profile."""
    config = load_config(path)
    assert config["substitutions"]["firmware_version"] == canonical_firmware_version()
    items = widgets(config)
    check_alert(items)
    check_power(items)
    check_environment(items)
    check_delivery(path)


def main():
    """Validate all profiles without network, device or provider access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    for name in PROFILES:
        verify(ROOT / "firmware" / name)
    logger.info(
        "HOME2_PRIMARY_STATE=PASS profiles=3 alerts=primary power=4 "
        "environment=2 provider_calls=0"
    )


if __name__ == "__main__":
    main()
