#!/usr/bin/env python3
"""Check weather touch targets and card containment from actual LCD YAML."""

import argparse
import importlib.util
import json
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
IDS = {
    "dashboard_weather_button",
    "weather_room_button",
    "fullscreen_weather_room_button",
    *(f"weather_full_{name}_button" for name in ("hourly", "daily", "details")),
    *(
        f"weather_{page}_tab_{tab}"
        for page in ("hourly", "daily", "details")
        for tab in ("now", "hourly", "daily", "details")
    ),
    *(f"weather_hour_window_{index}" for index in range(4)),
}


def load_config():
    """Reuse the reference loader that never resolves private YAML tags."""
    spec = importlib.util.spec_from_file_location(
        "lcd_reference", ROOT / "tests/lcd-navigation-static.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load_config()["lvgl"][0]


def walk(parent, page, origin=(0, 0)):
    """Retain parent bounds and absolute positions for every nested widget."""
    for item in parent.get("widgets", []):
        kind, widget = next(iter(item.items()))
        x = origin[0] + widget.get("x", 0)
        y = origin[1] + widget.get("y", 0)
        yield dict(
            kind=kind,
            widget=widget,
            parent=parent,
            page=page,
            x=x,
            y=y,
            parent_x=origin[0],
            parent_y=origin[1],
        )
        yield from walk(widget, page, (x, y))


def items():
    """Find controls and labels without treating boot-hidden cards as unused."""
    return {
        row["widget"]["id"]: row
        for page in load_config()["pages"]
        for row in walk(page, page["id"])
        if "id" in row["widget"]
    }


def rect(row):
    """Return a fixed rectangle; missing label heights must be made explicit."""
    widget = row["widget"]
    return row["x"], row["y"], widget["width"], widget["height"]


def overlap(first, second):
    """Treat touching edges as separate; any shared painted area is an overlap."""
    x, y, width, height = first
    other_x, other_y, other_width, other_height = second
    return (
        x < other_x + other_width
        and other_x < x + width
        and y < other_y + other_height
        and other_y < y + height
    )


def contained(row):
    """Include card bounds, not just the outer display bounds."""
    x, y, width, height = rect(row)
    parent = row["parent"]
    assert width > 0 and height > 0
    assert row["parent_x"] <= x and row["parent_y"] <= y
    assert x + width <= row["parent_x"] + parent.get("width", 1024)
    assert y + height <= row["parent_y"] + parent.get("height", 600)
    assert 0 <= x and x + width <= 1024 and 0 <= y and y + height <= 600


def check_controls(all_items):
    """Require finger-sized controls while preserving explicit page actions."""
    rows = []
    for identifier in sorted(IDS):
        row = all_items[identifier]
        widget = row["widget"]
        assert row["kind"] == "button", identifier
        assert widget["width"] >= 48 and widget["height"] >= 48, identifier
        contained(row)
        assert "on_press" not in widget, identifier
        rows.append(
            dict(
                id=identifier,
                page=row["page"],
                rect=list(rect(row)),
                parent=[
                    row["parent_x"],
                    row["parent_y"],
                    row["parent"].get("width", 1024),
                    row["parent"].get("height", 600),
                ],
                actionable=bool(widget.get("on_click")),
            )
        )
    return rows


def check_separation(all_items):
    """Guard reflowed text against buttons and keep neighboring cards separate."""
    room = all_items["fullscreen_weather_room_button"]
    for name in (
        "fullscreen_weather_wind_label",
        "fullscreen_weather_humidity_label",
        "fullscreen_weather_indoor_label",
    ):
        contained(all_items[name])
        assert not overlap(rect(all_items[name]), rect(room)), name
    provider = all_items["fullscreen_weather_provider_label"]
    attribution = all_items["weather_full_attribution_label"]
    for name in ("hourly", "daily", "details"):
        button = all_items[f"weather_full_{name}_button"]
        assert not overlap(rect(provider), rect(button)), name
        assert not overlap(rect(attribution), rect(button)), name
    for first in IDS:
        for second in IDS:
            if first >= second:
                continue
            a, b = all_items[first], all_items[second]
            if a["page"] == b["page"]:
                assert not overlap(rect(a), rect(b)), (first, second)
    card = room["parent"]
    assert 94 + card["height"] <= 342 - 8, "Full current card approaches day cards"


def evidence():
    """Produce bounded native-test inputs only after geometry has passed."""
    all_items = items()
    rows = check_controls(all_items)
    check_separation(all_items)
    return dict(
        status="PASS",
        controls=len(rows),
        rows=rows,
        physical_touch_tested=False,
        physical_visual_acceptance=False,
    )


def main():
    """Optionally save evidence; never overwrite an earlier result."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = evidence()
    if args.output:
        with args.output.open("x") as stream:
            stream.write(json.dumps(result, indent=2) + "\n")
    logger.info(
        "WEATHER_TOUCH_LAYOUT=PASS controls={} min48 contained=1 overlaps=0",
        result["controls"],
    )


if __name__ == "__main__":
    main()
