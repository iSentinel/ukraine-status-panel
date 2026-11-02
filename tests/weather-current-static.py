#!/usr/bin/env python3
"""Keep current-only weather reads small and hidden pages free of copies."""

import argparse
import re
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Check visibility before copies and retain one full forecast transaction."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "const auto weather_view = id(weather_client).snapshot();" not in source
    assert source.count("id(weather_client).current_snapshot()") >= 5
    assert source.count("id(weather_ui_snapshot) = id(weather_client).snapshot();") == 2
    for page in (9, 10):
        guard = f"if (!id(lcd_navigation_ready) || id(lcd_navigation_page) != {page}) return;"
        pattern = (
            re.escape(guard)
            + r"\n\s*const auto weather_view = id\(weather_client\).current_snapshot\(\);"
        )
        assert re.search(pattern, source), f"Page {page} copies before visibility"
    component = (ROOT / "components/usp_weather/weather_client.cpp").read_text()
    assert "const WeatherCurrent snapshot = this->current_snapshot();" in component
    assert "stack_free_b=%lu" in component
    logger.info(
        "WEATHER_CURRENT_STATIC=PASS compact5 full_transactions2 visible_before_copy"
    )


if __name__ == "__main__":
    main()
