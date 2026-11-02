#!/usr/bin/env python3
"""Verify power control touch sizes and separation from actual LCD YAML."""

import argparse
import importlib.util
import json
from pathlib import Path

from loguru import logger
import yaml

ROOT = Path(__file__).resolve().parents[1]
IDS = (
    "power_outage_group_minus",
    "power_outage_group_plus",
    "power_outage_refresh_button",
)


def load(name, path):
    """Reuse reference readers without resolving secret values."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def config(path):
    """Read tags as plain reference data, without opening secret files."""
    reference = load("lcd_reference", ROOT / "tests/lcd-navigation-static.py")
    reference.load_config()
    return yaml.load(path.read_text(), Loader=reference.ReferenceLoader)


def items(path):
    """Keep actual parent bounds and absolute widget coordinates."""
    geometry = load("weather_layout", ROOT / "tests/weather-touch-layout.py")
    return {
        row["widget"]["id"]: row
        for page in config(path)["lvgl"][0]["pages"]
        for row in geometry.walk(page, page["id"])
        if "id" in row["widget"]
    }


def check_neighbors(rows):
    """Keep the controls away from existing status and timeline text."""
    minus, plus, refresh = [rows[name]["widget"] for name in IDS]
    status = rows["i18n_043"]["widget"]
    description = rows["i18n_044"]["widget"]
    assert status["x"] + status["width"] <= minus["x"] - 8
    assert minus["x"] + minus["width"] <= plus["x"] - 8
    assert (
        max(minus["y"] + minus["height"], plus["y"] + plus["height"])
        <= description["y"] - 8
    )
    parent = rows[IDS[2]]["parent"]
    ticks = [
        item["label"]["y"]
        for item in parent["widgets"]
        if "label" in item
        and item["label"].get("text") in ("00", "06", "12", "18", "24")
    ]
    assert len(ticks) == 5 and refresh["y"] + refresh["height"] <= min(ticks) - 10
    provider = rows["power_outage_provider_label"]["widget"]
    assert provider["x"] + provider["width"] <= refresh["x"] - 8


def evidence(source=ROOT / "firmware/ukraine-status-panel.yaml"):
    """Require three usable controls without changing their actions."""
    geometry = load("weather_layout", ROOT / "tests/weather-touch-layout.py")
    all_items = items(source)
    result = []
    for name in IDS:
        row = all_items[name]
        widget, parent = row["widget"], row["parent"]
        assert row["kind"] == "button" and widget.get("on_click"), name
        assert widget["width"] >= 48 and widget["height"] >= 48, name
        geometry.contained(row)
        result.append(
            dict(
                id=name,
                page=row["page"],
                actionable=True,
                rect=list(geometry.rect(row)),
                parent=[
                    row["parent_x"],
                    row["parent_y"],
                    parent["width"],
                    parent["height"],
                ],
            )
        )
    assert not geometry.overlap(
        geometry.rect(all_items[IDS[0]]), geometry.rect(all_items[IDS[1]])
    )
    check_neighbors(all_items)
    return dict(
        status="PASS",
        controls=3,
        rows=result,
        physical_touch_tested=False,
        device_contact=False,
    )


def main():
    """Expose a repeatable layout check with optional new evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-yaml", type=Path, default=ROOT / "firmware/ukraine-status-panel.yaml"
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.exists():
        parser.error("Evidence already exists")
    result = evidence(args.source_yaml)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    logger.info("POWER_TOUCH_LAYOUT=PASS controls=3 min48 gaps8_10 contained")


if __name__ == "__main__":
    main()
