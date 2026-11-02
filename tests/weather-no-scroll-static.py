#!/usr/bin/env python3
"""Guard Weather cards against scrollbars and overflowing content."""

import importlib.util
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML_PATH = ROOT / "firmware/ukraine-status-panel.yaml"


def load_config():
    """Reuse the reference-only YAML loader from LCD navigation tests."""
    path = ROOT / "tests/lcd-navigation-static.py"
    spec = importlib.util.spec_from_file_location("lcd_nav_static", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load_config()


def page_by_id(config, page_id):
    """Return an LVGL page by stable ID."""
    pages = config["lvgl"][0]["pages"]
    return next(page for page in pages if page.get("id") == page_id)


def fixed_cards(page):
    """Return top-level fixed object cards on a Weather page."""
    cards = []
    for item in page["widgets"]:
        if "obj" in item:
            cards.append(item["obj"])
    return cards


def child_by_id(cards, widget_id):
    """Find a nested child by stable ID inside fixed cards."""
    for card in cards:
        for item in card.get("widgets", []):
            value = next(iter(item.values()))
            if value.get("id") == widget_id:
                return value
    raise AssertionError(f"missing Weather widget: {widget_id}")


def check_page(config, page_id, prefix, normal):
    """Require six non-scroll cards and bounded long labels."""
    page = page_by_id(config, page_id)
    cards = fixed_cards(page)
    assert len(cards) == 6, f"{page_id}: expected six fixed cards, got {len(cards)}"
    assert all(
        card.get("scrollable") is False for card in cards
    ), f"{page_id}: every Weather frame must be non-scrollable"

    condition_width = 198 if normal else 196
    for index in range(4):
        condition = child_by_id(cards, f"{prefix}{index}_condition")
        rain = child_by_id(cards, f"{prefix}{index}_rain")
        assert condition["width"] == condition_width
        assert condition["height"] >= 34
        assert condition["long_mode"] == "WRAP"
        assert rain["width"] == condition_width
        assert rain["height"] >= 28
        assert rain["long_mode"] == "WRAP"
        assert "SCROLL" not in str(condition.get("long_mode", ""))
        assert "SCROLL" not in str(rain.get("long_mode", ""))
        assert rain["text"] == "Precip: --%"


def check_detail_labels(config):
    """Long localized/current-condition strings must have bounded wrapping."""
    normal = fixed_cards(page_by_id(config, "page_weather"))
    fullscreen = fixed_cards(page_by_id(config, "page_weather_full"))
    requirements = {
        "weather_condition_label": (normal, 244, 54, "WRAP"),
        "weather_indoor_label": (normal, 320, 42, "WRAP"),
        "fullscreen_weather_condition_label": (fullscreen, 270, 52, "WRAP"),
        "fullscreen_weather_indoor_label": (fullscreen, 280, 28, "DOT"),
        "fullscreen_weather_updated_label": (fullscreen, 430, 34, "WRAP"),
        "weather_full_attribution_label": (fullscreen, 430, 28, "DOT"),
    }
    for widget_id, (cards, width, height, long_mode) in requirements.items():
        widget = child_by_id(cards, widget_id)
        assert widget["width"] == width
        assert widget["height"] == height
        assert widget["long_mode"] == long_mode

    room = child_by_id(normal, "weather_room_button")
    room_full = child_by_id(fullscreen, "fullscreen_weather_room_button")
    assert (room["width"], room["height"]) == (156, 48)
    assert (room_full["width"], room_full["height"]) == (135, 48)


def check_runtime_copy():
    """Keep runtime strings compact and prohibit scrolling long modes."""
    source = YAML_PATH.read_text()
    helper = (YAML_PATH.parent.parent / "include/usp_weather_view.h").read_text()
    for prefix in ("Опади: ", "Осадки: ", "Precip: "):
        assert prefix in helper
    for index in range(4):
        assert f"return id(weather_ui_view).days[{index}].rain;" in source
    weather = source[
        source.index("# WEATHER / OPEN-METEO") : source.index(
            "# MAP", source.index("# WEATHER / FULLSCREEN")
        )
    ]
    assert "long_mode: SCROLL" not in weather
    assert "long_mode: SCROLL_CIRCULAR" not in weather


def main():
    """Validate both Weather layouts."""
    config = load_config()
    check_page(config, "page_weather", "weather_day", True)
    check_page(config, "page_weather_full", "full_weather_day", False)
    check_detail_labels(config)
    check_runtime_copy()
    logger.info("WEATHER_NO_SCROLL_STATIC=PASS cards=12 content=bounded")


if __name__ == "__main__":
    main()
