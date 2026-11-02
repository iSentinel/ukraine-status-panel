#!/usr/bin/env python3
"""Validate the M6 Room page and honest 24-hour graph integration."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
CHART = ROOT / "include/usp_room_chart.h"
CHART_MODEL = ROOT / "include/usp_room_chart_model.h"
PERIOD = ROOT / "include/usp_room_period_chart.h"
SWIPE = ROOT / "include/usp_swipe_navigation.h"


def need(value, message):
    """Fail with a readable contract error."""
    if not value:
        raise RuntimeError(message)


def main():
    """Keep Room isolated, bounded and based only on real SHT history."""
    yaml = YAML.read_text()
    chart = CHART.read_text()
    chart_model = CHART_MODEL.read_text()
    period = PERIOD.read_text()
    swipe = SWIPE.read_text()
    need(CURRENT_FIRMWARE_MARKER in yaml, "wrong version")
    need("- include/usp_room_chart.h" in yaml, "chart include missing")
    need("id: page_room" in yaml and "page: 10" in yaml, "Room route missing")
    need("id: weather_room_button" in yaml, "Weather Room entry missing")
    need("id: fullscreen_weather_room_button" in yaml,
         "fullscreen Weather Room entry missing")
    need("id: refresh_room_current_ui" in yaml, "current refresh missing")
    need("id: refresh_room_graph_ui" in yaml, "graph refresh missing")
    need(yaml.count("id: room_temp_line_") == 12, "temperature segments != 12")
    need(yaml.count("id: room_humidity_line_") == 12, "humidity segments != 12")
    need("id(lcd_navigation_page) != 10" in yaml, "visible-page guard missing")
    need("schedule_room_graph_refresh" in yaml, "sample graph hook missing")
    need("MALLOC_CAP_SPIRAM" in chart and "allocate_workspace" in chart,
         "graph workspace must prefer PSRAM")
    need("using Line = esphome::lvgl::LvLineType" in chart and "line->obj" in chart,
         "LVGL line wrapper adapter missing")
    need("usp_room_chart::build(*id(room_history)" in yaml,
         "RoomHistory pointer must be dereferenced for chart model")
    need("MAX_POINTS = 372" in chart_model and "MAX_SEGMENTS = 12" in chart_model,
         "graph bounds changed")
    need("366U" in period and "DAYS_7" in period and "DAYS_30" in period,
         "aggregate period windows missing")
    need("BREAK_SECONDS" in chart and "dropped_runs" in chart,
         "gap/discontinuity contract missing")
    need("MAIN_PAGES[] = {0, 1, 2, 3, 4, 12, 9}" in swipe,
         "Room must not change the main swipe circle")
    need("if (page == 10)" in swipe, "Room fullscreen exit missing")
    start = yaml.index("      - id: page_room")
    end = yaml.index("\nscript:", start)
    room = yaml[start:end].lower()
    for token in ("pressure", "canvas", "fake", "demo", "random"):
        need(token not in room, f"forbidden Room UI token: {token}")
    print(
        "ROOM_PAGE_STATIC=PASS page10=1 entries=2 segments=12x2 "
        "psram=1 gaps=1 web_growth=0"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"ROOM_PAGE_STATIC=FAIL {exc}", file=sys.stderr)
        raise
