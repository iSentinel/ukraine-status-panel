#!/usr/bin/env python3
"""Verify WX-5C provider day/night and bounded icon rendering wiring."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "components/usp_weather/weather_client.h").read_text()
CPP = (ROOT / "components/usp_weather/weather_client.cpp").read_text()
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
MODEL = (ROOT / "include/usp_weather_icon_model.h").read_text()
VIEW = (ROOT / "include/usp_forecast_view.h").read_text()


def main() -> None:
    assert "bool is_day{false};" in HEADER
    assert 'hourly["is_day"][index]' in CPP
    assert 'weather_code,is_day,wind_speed_10m' in CPP
    assert "hour.is_day" in VIEW
    for token in ("Shape::SUN", "Shape::CRESCENT", "Shape::CLOUD", "Shape::SNOW", "Shape::THUNDER"):
        assert token in MODEL
    for color in ("0xFACC15U", "0xF97316U", "0x93C5FDU", "0x60A5FAU", "0x94A3B8U", "0x38BDF8U", "0xF8FAFCU", "0xFBBF24U"):
        assert color in MODEL
    assert YAML.count("_moon_outer") >= 12  # six ids + six render references
    assert YAML.count("_moon_inner") >= 12
    for index in range(6):
        for suffix in ("icon", "drops", "moon_outer", "moon_inner"):
            assert f"weather_hour_{index}_{suffix}" in YAML
    for index in range(7):
        for suffix in ("icon", "drops"):
            assert f"weather_daily_{index}_{suffix}" in YAML
    assert "usp_weather_icon_model::glyph(style.shape)" in YAML
    assert "style.rain_drops" in YAML
    assert "Shape::CRESCENT" in YAML
    print("WEATHER_ICONS_STATIC=PASS provider_is_day=required hourly_icons=6 daily_icons=7 crescent=primitive rain=blue_drops")


if __name__ == "__main__":
    main()
