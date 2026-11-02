#!/usr/bin/env python3
"""Check that Home header information occupies separate, bounded rectangles."""
import argparse
import importlib.util
import itertools
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
NAMES = (
    "dashboard_brand_label",
    "header_date_label",
    "header_clock_label",
    "dashboard_wifi_badge",
    "dashboard_header_mains_caption",
    "dashboard_top_grid_label",
    "dashboard_language_label",
)


def load(name, path):
    """Import the established inert configuration reader."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def widgets():
    """Read positioned widgets, never a similarly named update action."""
    reader = load("settings_layout", ROOT / "tests/settings-touch-layout.py")
    config = reader.config(ROOT / "firmware/ukraine-status-panel.yaml")
    page = next(
        p for p in config["lvgl"][0]["pages"] if p["id"] == "page_home"
    )
    rows = {}
    for widget in page["widgets"]:
        for kind in ("label", "obj"):
            row = widget.get(kind)
            if row is not None and row.get("id") in NAMES:
                rows[row["id"]] = row
    return rows


def verify():
    """Reject overlap, implicit widths and header growth into map cards."""
    rows = widgets()
    assert set(rows) == set(NAMES)
    for name, row in rows.items():
        expected_pad = 5 if name == "dashboard_wifi_badge" else 0
        assert row["pad_all"] == expected_pad, name
        assert row["width"] > 0 and row["height"] > 0, name
        assert 12 <= row["x"] < row["x"] + row["width"] <= 1012, name
        assert 0 <= row["y"] < row["y"] + row["height"] <= 56, name
        assert row["long_mode"] == "DOT", name
        assert not any(k.startswith("on_") for k in row), name
    for first, second in itertools.combinations(rows.values(), 2):
        left, right = sorted((first, second), key=lambda r: r["x"])
        assert left["x"] + left["width"] + 8 <= right["x"]
    wifi = rows["dashboard_wifi_badge"]
    assert wifi["bg_opa"] == "100%"
    assert wifi["border_width"] == 1
    assert wifi["text"] == "WiFi"
    assert rows["header_clock_label"]["text"] == "--:--:--"
    assert rows["header_date_label"]["text"] == "--.--.----"
    return rows


def main():
    """Validate source layout without contacting the panel."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    verify()
    logger.info("HOME_HEADER_LAYOUT=PASS slots7 gap8 map_unchanged")


if __name__ == "__main__":
    main()
