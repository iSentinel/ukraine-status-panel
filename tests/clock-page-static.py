#!/usr/bin/env python3
"""Guard the M5 Clock page, time truth model and shared-map architecture."""

import importlib.util
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML_PATH = ROOT / "firmware/ukraine-status-panel.yaml"


def load_config():
    """Reuse the reference-only loader used by LCD navigation tests."""
    path = ROOT / "tests/lcd-navigation-static.py"
    spec = importlib.util.spec_from_file_location("lcd_nav_static", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load_config()


def page_by_id(config, page_id):
    """Return an LVGL page by its stable ID."""
    pages = config["lvgl"][0]["pages"]
    return next(page for page in pages if page.get("id") == page_id)


def top_cards(page):
    """Return the fixed Clock cards."""
    return [item["obj"] for item in page["widgets"] if "obj" in item]


def check_page(config):
    """Require a bounded non-scroll Clock layout in button mode."""
    page = page_by_id(config, "page_clock")
    assert page.get("scrollable") is False
    actions = page["on_load"]
    expected = {"script.execute": {"id": "sync_lcd_navigation_page", "page": 9}}
    assert expected in actions
    assert {"script.execute": "refresh_clock_time"} in actions
    assert {"script.execute": "refresh_clock_context"} in actions
    cards = top_cards(page)
    assert [card["id"] for card in cards] == [
        "clock_time_card", "clock_weather_card", "clock_map_card"
    ]
    assert all(card.get("scrollable") is False for card in cards)
    assert all(card["y"] + card["height"] <= 524 for card in cards)


def check_time_contract(config, source):
    """Require Kyiv SNTP, truthful placeholders and minute-only text changes."""
    time_cfg = next(item for item in config["time"] if item.get("id") == "panel_time")
    assert time_cfg["platform"] == "sntp"
    assert time_cfg["timezone"] == "Europe/Kyiv"
    sync = str(time_cfg["on_time_sync"])
    assert "clock_sntp_synced" in sync and "refresh_clock_time" in sync
    assert "id(clock_sntp_synced) && now.is_valid()" in source
    assert "usp_clock::minute_gate().changed" in source
    clock = (ROOT / "include/usp_clock_presentation.h").read_text()
    assert '"--:--"' in clock
    assert "Waiting for time sync" in clock


def check_navigation_and_layout(source):
    """Keep Clock in the seven-page swipe cycle and support fullscreen geometry."""
    gesture = (ROOT / "include/usp_swipe_navigation.h").read_text()
    assert "constexpr uint8_t MAIN_PAGES[] = {0, 1, 2, 3, 4, 12, 9};" in gesture
    assert "lambda: return page == 9;" in source
    assert "id: page_clock" in source
    assert "id(lcd_nav_clock)" in source
    assert "const char *region = usp_map_touch::region_name" in source
    assert "id(clock_region_name), region.c_str()" not in source
    assert "height(id(clock_map_card), full ? 502 : 452);" in source
    assert "height(id(clock_weather_card), full ? 242 : 192);" in source


def check_shared_map(source):
    """Clock must mirror the resident dashboard map without a second PSRAM buffer."""
    cache = (ROOT / "include/usp_dashboard_map_cache.h").read_text()
    marker = "Share existing pixels with Clock; never allocate another map buffer."
    assert marker in cache
    assert "bool mirror_to(" in cache
    assert "Cache::instance().mirror_to(" in source
    assert cache.count("heap_caps_aligned_alloc(") == 1
    assert "id(panel_lvgl).invalidate_home_native_frame_cache();" in source


def main():
    """Validate the source-only M5 Clock release contract."""
    source = YAML_PATH.read_text()
    config = load_config()
    check_page(config)
    check_time_contract(config, source)
    check_navigation_and_layout(source)
    check_shared_map(source)
    logger.info(
        "CLOCK_PAGE_STATIC=PASS page9 minute_gate Kyiv shared_map seven_page_cycle"
    )


if __name__ == "__main__":
    main()
