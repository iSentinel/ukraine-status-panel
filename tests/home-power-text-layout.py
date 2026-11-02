#!/usr/bin/env python3
"""Check Home 2.0 power values fit inside the primary power card."""

import argparse
import importlib.util
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
PRIMARY = ("home_grid_status_label", "dashboard_shelly_summary_label")
SECONDARY = ("dashboard_battery_label", "dashboard_wifi_label")
NAMES = PRIMARY + SECONDARY


def load_config():
    """Load the source profile with the project's reference loader."""
    source = ROOT / "tests/settings-touch-layout.py"
    spec = importlib.util.spec_from_file_location("layout", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.config(ROOT / "firmware/ukraine-status-panel.yaml")


def power_card(config):
    """Return the Home power card payload."""
    page = next(
        item
        for item in config["lvgl"][0]["pages"]
        if item["id"] == "page_home"
    )
    return next(
        entry["obj"]
        for entry in page["widgets"]
        if entry.get("obj", {}).get("id") == "dashboard_power_card"
    )


def verify():
    """Reject clipped values, overlap and incorrect type hierarchy."""
    card = power_card(load_config())
    labels = {
        entry["label"]["id"]: entry["label"]
        for entry in card["widgets"]
        if "label" in entry and entry["label"].get("id") in NAMES
    }
    assert set(labels) == set(NAMES)
    for name in PRIMARY:
        assert labels[name]["text_font"] == "usp_i18n_20", name
        assert labels[name]["height"] == 36, name
    for name in SECONDARY:
        assert labels[name]["text_font"] == "usp_i18n_18", name
        assert labels[name]["height"] == 34, name
    for name, row in labels.items():
        assert row["pad_all"] == 0, name
        assert row["x"] + row["width"] <= card["width"] - 2, name
        assert row["y"] + row["height"] <= card["height"] - 2, name
    pairs = ((PRIMARY[0], PRIMARY[1]), (SECONDARY[0], SECONDARY[1]))
    for left, right in pairs:
        right_edge = labels[left]["x"] + labels[left]["width"]
        assert right_edge <= labels[right]["x"]
    primary_bottom = labels[PRIMARY[0]]["y"] + labels[PRIMARY[0]]["height"]
    assert primary_bottom <= labels[SECONDARY[0]]["y"]


def main():
    """Run source geometry checks without contacting the device."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    verify()
    logger.info(
        "HOME_POWER_TEXT_LAYOUT=PASS values4 mains_shelly_battery_wifi "
        "contained no_overlap"
    )


if __name__ == "__main__":
    main()
