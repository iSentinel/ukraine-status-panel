#!/usr/bin/env python3
"""Require a consistent weather view throughout yielded LCD update batches."""

import argparse
import re
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def script(source, name):
    """Extract one script and leave later scripts outside the contract."""
    start = source.index(f"  - id: {name}\n")
    end = source.find("\n  - id: ", start + 1)
    return source[start : end if end >= 0 else len(source)]


def check_time_caption(source, weather):
    """Require the shared view and parser to use the same strict time helper."""
    assert "include/usp_weather_view.h" in source
    header = (ROOT / "include/usp_weather_view.h").read_text()
    assert "usp_weather_time::as_of(snapshot.updated_time, language)" in header
    assert "return id(weather_ui_view).updated;" in weather
    component = (ROOT / "components/usp_weather/weather_client.cpp").read_text()
    assert "return usp_weather_time::valid_timestamp(text);" in component


def check_shared_view(source, weather):
    """Bind yielded ordinary/fullscreen writes to the same owning transaction."""
    capture = "id(weather_ui_snapshot) = id(weather_client).snapshot();"
    assert weather.count(capture) == 1
    assert weather.count("id(weather_ui_view) = usp_weather_view::build(") == 1
    assert weather.index(capture) < weather.index("usp_weather_view::build(")
    assert weather.index("usp_weather_view::build(") < weather.index(
        "id: home_weather_city_label"
    )
    assert "id(weather_ui_snapshot), id(ui_language)" in weather
    assert weather.count("return id(weather_ui_view).") == 26
    assert "return id(weather_ui_view).days[3].rain;" in weather
    assert weather.count("delay: 1ms") >= 5
    assert "id: weather_ui_view" in source and "id: weather_ui_snapshot" in source
    mirror = script(source, "refresh_weather_fullscreen_ui")
    assert "lv_label_get_text" not in mirror
    assert mirror.count("usp_weather_lcd_delivery::render(") == 1
    assert mirror.count("usp_weather_lcd_delivery::Targets targets{") == 1
    assert "id(weather_ui_view)" in mirror
    assert "weather_ui_snapshot" not in mirror
    delivery = (ROOT / "include/usp_weather_lcd_delivery.h").read_text()
    assert delivery.count("lv_label_set_text(") == 12
    assert delivery.count(".c_str()") == 12
    assert delivery.count("lv_obj_set_style_text_color(") == 1
    assert "view.status_color" in delivery
    assert "id(weather_ui_view).status_color" in weather


def main():
    """Reject mixed getter reads and accidental refresh/layout changes."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    getters = (
        "weather_valid",
        "weather_stale",
        "last_fetch_ok",
        "city_name",
        "country_name",
        "temperature_c",
        "weather_code",
        "forecast_count",
        "forecast_date",
        "forecast_condition_text",
        "forecast_high_c",
        "forecast_low_c",
        "forecast_precip_percent",
        "updated_time",
        "apparent_temperature_c",
        "humidity_percent",
        "wind_kmh",
        "is_day",
        "condition_text",
    )
    pattern = r"id\(weather_client\)\.(" + "|".join(getters) + r")\("
    assert not re.search(pattern, source), "LCD or template still mixes getters"
    weather = script(source, "refresh_weather_ui")
    check_time_caption(source, weather)
    check_shared_view(source, weather)
    logger.info("WEATHER_SNAPSHOT_STATIC=PASS one view across yielded LCD batches")


if __name__ == "__main__":
    main()
