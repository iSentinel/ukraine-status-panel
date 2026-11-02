#!/usr/bin/env python3
"""Protect accepted Weather card geometry and actions across shared styles."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/weather-card-layout.json"
PROFILES = (
    "ukraine-status-panel.yaml",
    "ukraine-status-panel-public-first-flash.yaml",
    "ukraine-status-panel-production.yaml",
)
PAGES = ("page_weather", "page_weather_full")


def loader():
    """Reuse the tag-safe project loader without reading secret files."""
    path = ROOT / "tests/lcd-navigation-static.py"
    spec = importlib.util.spec_from_file_location("navigation_reference", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ReferenceLoader.add_multi_constructor("!", module.unknown_tag)
    return module.yaml, module.ReferenceLoader


def cards(text):
    """Return complete card subtrees after ordinary YAML merges resolve."""
    yaml, reference = loader()
    config = yaml.load(text, Loader=reference)
    lvgl = config["lvgl"][0]
    result = {}
    for page in lvgl["pages"]:
        if page["id"] in PAGES:
            result[page["id"]] = [
                item["obj"] for item in page["widgets"] if "obj" in item
            ]
    assert set(result) == set(PAGES)
    assert all(len(value) == 6 for value in result.values())
    return result


def verify(text, expected):
    """Compare all style, geometry, child labels and button actions."""
    actual = cards(text)
    assert actual == expected, "Accepted Weather card structure changed."


def negative_controls(text, expected):
    """Require four valid YAML changes to fail the accepted-card comparison."""
    start = text.index("      - id: page_weather\n")
    end = text.index("      - id: page_map_full\n", start)
    body = text[start:end]
    changes = (
        ("scrollable: false", "scrollable: true"),
        ("bg_color: 0x0C1828", "bg_color: 0x0C1829"),
        ("width: 420", "width: 421"),
        ("page: 10", "page: 9"),
    )
    for old, new in changes:
        assert old in body, old
        variant = text[:start] + body.replace(old, new, 1) + text[end:]
        assert cards(variant) != expected, old
    return len(changes)


def main():
    """Check explicit previews or the three ordinary firmware profiles."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, action="append")
    parser.add_argument("--fixture", type=Path, default=FIXTURE)
    parser.add_argument("--negative-controls", action="store_true")
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    assert fixture["baseline_version"] == "0.115.376-dev"
    paths = args.config or [ROOT / "firmware" / name for name in PROFILES]
    controls = 0
    for path in paths:
        text = path.read_text()
        verify(text, fixture["cards"])
        if args.negative_controls:
            controls += negative_controls(text, fixture["cards"])
    logger.info(
        "WEATHER_CARD_LAYOUT=PASS profiles={} cards={} negative={}",
        len(paths),
        len(PAGES) * 6,
        controls,
    )


if __name__ == "__main__":
    main()
