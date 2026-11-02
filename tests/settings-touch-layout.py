#!/usr/bin/env python3
"""Check Settings targets, panel bounds and co-visible overlap from YAML."""
import argparse
import importlib.util
import itertools
import json
from pathlib import Path

import yaml
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
IDS = (
    "web_password_reset_ui_button",
    "settings_sound_toggle_button",
    "settings_buzzer_test_button",
    "palette_standard_button",
    "palette_accessible_button",
    "palette_mono_button",
    "lcd_navigation_mode_button",
    "display_off_button",
    "panel_restart_ui_button",
    "lang_ua_button",
    "lang_ru_button",
    "lang_en_button",
    "settings_region_choose",
    "i18n_073",
    *(
        f"settings_section_{name}"
        for name in (
            "display",
            "language",
            "region",
            "sound",
            "network",
            "system",
        )
    ),
)


def load(name, path):
    """Import an existing geometry reader without resolving secrets."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def config(path):
    """Use the existing YAML loader with inert ESPHome tags."""
    reference = load("lcd_reference", ROOT / "tests/lcd-navigation-static.py")
    reference.load_config()
    return yaml.load(path.read_text(), Loader=reference.ReferenceLoader)


def items(path):
    """Read absolute coordinates while retaining each visibility group."""
    geometry = load("weather_layout", ROOT / "tests/weather-touch-layout.py")
    return {
        row["widget"]["id"]: row
        for page in config(path)["lvgl"][0]["pages"]
        for row in geometry.walk(page, page["id"])
        if "id" in row["widget"]
    }


def gap(left, right):
    """Return the empty horizontal space between neighboring controls."""
    return right["x"] - left["x"] - left["width"]


def check_controls(rows, geometry):
    """Require accessible touch rectangles and established click actions."""
    result = []
    for name in IDS:
        row = rows[name]
        widget = row["widget"]
        assert row["kind"] == "button" and widget.get("on_click"), name
        assert widget["width"] >= 48 and widget["height"] >= 48, name
        geometry.contained(row)
        assert row["y"] + widget["height"] <= 526, name
        result.append(
            dict(
                id=name,
                page=row["page"],
                rect=list(geometry.rect(row)),
                visibility_group=row["parent"].get("id"),
            )
        )
    slider = rows["settings_backlight_slider"]
    geometry.contained(slider)
    assert slider["widget"]["height"] >= 48
    assert slider["widget"]["min_value"] == 10
    assert slider["widget"]["max_value"] == 100
    assert "light.turn_on" in str(slider["widget"]["on_release"])
    return result


def check_separation(rows, geometry):
    """Compare controls only when their actual parent panels can coexist."""
    for first, second in itertools.combinations(IDS, 2):
        a, b = rows[first], rows[second]
        if a["parent"].get("id") != b["parent"].get("id"):
            continue
        assert not geometry.overlap(geometry.rect(a), geometry.rect(b)), (
            first,
            second,
        )
    for first, second in (
        ("settings_sound_toggle_button", "settings_buzzer_test_button"),
        ("palette_standard_button", "palette_accessible_button"),
        ("palette_accessible_button", "palette_mono_button"),
    ):
        assert gap(rows[first]["widget"], rows[second]["widget"]) >= 8
    sidebar = rows["settings_sidebar"]
    for name in (
        "display",
        "language",
        "region",
        "sound",
        "network",
        "system",
    ):
        panel = rows["settings_" + name + "_panel"]
        geometry.contained(panel)
        assert panel["widget"]["pad_all"] == 0
        assert panel["widget"]["hidden"] == (name != "display")
        assert not geometry.overlap(
            geometry.rect(sidebar), geometry.rect(panel)
        )
        assert panel["y"] + panel["widget"]["height"] <= 526


def evidence(source=ROOT / "firmware/ukraine-status-panel.yaml"):
    """Produce source-derived inputs for the real LVGL hit-area checks."""
    geometry = load("weather_layout", ROOT / "tests/weather-touch-layout.py")
    rows = items(source)
    result = check_controls(rows, geometry)
    check_separation(rows, geometry)
    return dict(
        status="PASS",
        controls=len(result),
        minimum_height=48,
        rows=result,
        sections=6,
        physical_touch_tested=False,
        device_contact=False,
    )


def main():
    """Optionally save a new receipt; never overwrite earlier evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-yaml",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel.yaml",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = evidence(args.source_yaml)
    if args.output:
        with args.output.open("x") as output:
            json.dump(result, output, indent=2)
            output.write("\n")
    logger.info(
        "SETTINGS_TOUCH_LAYOUT=PASS controls={} min48 panels6",
        result["controls"],
    )


if __name__ == "__main__":
    main()
