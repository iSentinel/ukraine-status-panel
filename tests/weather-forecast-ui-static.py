#!/usr/bin/env python3
"""Fail closed on WX-5B hourly/seven-day LCD wiring."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
HEADER = (ROOT / "include/usp_forecast_chart.h").read_text()


def main() -> None:
    assert "- id: page_weather_hourly" in YAML
    assert "- id: page_weather_daily" in YAML
    assert YAML.count("id: weather_hourly_line_") == 12
    for index in range(6):
        for field in ("time", "temp", "condition", "rain"):
            assert f"id: weather_hour_{index}_{field}" in YAML
    for index in range(7):
        for field in ("date", "temp", "condition", "rain"):
            assert f"id: weather_daily_{index}_{field}" in YAML
    for index in range(4):
        assert f"id: weather_hour_window_{index}" in YAML
        assert f"id(weather_forecast_window) = {index};" in YAML
    assert "id: weather_full_hourly_button" in YAML
    assert "id: weather_full_daily_button" in YAML
    assert "id: weather_full_details_button" in YAML
    assert "- id: page_weather_details" in YAML
    for page in ("hourly", "daily", "details"):
        assert f"id: weather_{page}_tab_details" in YAML
    assert "id: refresh_weather_forecast_ui" in YAML
    assert "id(weather_ui_snapshot) = id(weather_client).snapshot();" in YAML
    assert "usp_forecast_view::build(" in YAML
    assert "usp_forecast_chart::render(view, lines, &chart_buffers, 920, 130);" in YAML
    assert "if (page == 7) page = 2;" in YAML
    assert "MAX_POINTS = 24" in HEADER and "MAX_SEGMENTS = 12" in HEADER
    assert "if (!source.available)" in HEADER
    assert "lv_obj_add_flag(lines[index]->obj, LV_OBJ_FLAG_HIDDEN)" in HEADER
    print("WEATHER_FORECAST_UI_STATIC=PASS hourly24 windows4 cards6 daily7 gaps12 page7_swipe_contract")


if __name__ == "__main__":
    main()
