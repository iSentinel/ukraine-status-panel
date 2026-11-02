#!/usr/bin/env python3
"""Fail closed on WX-5D optional detail semantics and Details-page wiring."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
HEADER = (ROOT / "components/usp_weather/weather_client.h").read_text()
CPP = (ROOT / "components/usp_weather/weather_client.cpp").read_text()
DETAILS = (ROOT / "include/usp_forecast_details_view.h").read_text()

def main() -> None:
    for field in ("apparent_temperature_c", "precip_mm", "wind_kmh",
                  "wind_direction_deg", "gust_kmh", "pressure_hpa", "uv_index"):
        assert f"float {field}{{std::numeric_limits<float>::quiet_NaN()}};" in HEADER
    for core in ('temperature_2m', 'precipitation_probability', 'weather_code', 'is_day'):
        assert f'hourly["{core}"][index]' in CPP
    assert 'auto optional = [&](const char *key' in CPP
    for optional in ("apparent_temperature", "precipitation", "wind_speed_10m",
                     "wind_direction_10m", "wind_gusts_10m", "surface_pressure", "uv_index"):
        assert f'optional("{optional}"' in CPP
    assert "static_assert(sizeof(View) <= 256" in DETAILS
    assert "std::isfinite(value)" in DETAILS and '"--"' in DETAILS
    assert "- include/usp_forecast_details_view.h" in YAML
    assert "type: usp_forecast_details::View" in YAML
    assert "id(weather_details_view) = usp_forecast_details::build(" in YAML
    assert "- id: page_weather_details" in YAML
    for suffix in ("feels", "precip", "wind", "pressure", "uv"):
        assert f"id: weather_details_{suffix}_value" in YAML
    for suffix in ("day_precip", "sunrise", "sunset", "uvmax"):
        assert f"id: weather_details_{suffix}_value" in YAML
    for page in ("hourly", "daily", "details"):
        assert f"id: weather_{page}_tab_details" in YAML
    assert "id: weather_full_details_button" in YAML
    assert "if (page == 7) page = 2;" in YAML
    assert "scrollable: false" in YAML[YAML.index("- id: page_weather_details"):YAML.index("# MAP", YAML.index("- id: page_weather_details"))]
    print("WEATHER_DETAILS_STATIC=PASS optional7 core4 details256 page4 swipe_contract no_scroll")

if __name__ == "__main__":
    main()
