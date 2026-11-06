#!/usr/bin/env python3
"""Verify the full-map button's touch area and its exact Home catalog binding."""
import argparse
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()


def widget_block(identifier: str, lines: int = 20) -> str:
    """Read the existing widget definition without resolving configuration tags."""
    start = YAML.index(f"id: {identifier}\n")
    return "\n".join(YAML[start:].splitlines()[:lines])


def integer(block: str, key: str) -> int:
    """Require a concrete numeric geometry property."""
    match = re.search(rf"^\s*{key}:\s*(\d+)\s*$", block, re.M)
    assert match, (key, block)
    return int(match.group(1))


def caption_bindings(home: str) -> None:
    """Require all three real button updates to use the correct caption key."""
    expression = (
        "return usp_home_page_text::label(id(ui_language), "
        "usp_home_page_text::Label::MAP_EXPAND);"
    )
    pattern = (
        r"id: dashboard_map_expand_button\n\s+text: !lambda \|-\n\s+"
        + re.escape(expression)
    )
    assert len(re.findall(pattern, home)) == 3
    assert home.count("id: dashboard_map_expand_button") == 3


def main() -> None:
    """Preserve every width, height, spacing and design-rule check."""
    button = widget_block("dashboard_map_expand_button", 24)
    region = widget_block("dashboard_region_label", 16)
    card = widget_block("dashboard_map_card", 16)
    width, height = integer(button, "width"), integer(button, "height")
    x, y = integer(button, "x"), integer(button, "y")
    rx, rw = integer(region, "x"), integer(region, "width")
    card_width = integer(card, "width")
    assert width >= 80, width
    assert height >= 48, height
    assert y <= 4, y
    assert x - (rx + rw) >= 8, (x, rx, rw)
    assert card_width - (x + width) >= 8, (card_width, x, width)
    assert 'text: "MAX"' not in button
    start = YAML.index("id: localize_page_home\n")
    end = YAML.index("id: localize_page_weather\n", start)
    caption_bindings(YAML[start:end])
    assert "include/usp_home_page_text.h" in YAML
    review = (
        ROOT / "docs/DESIGN_TOUCH_TARGET_RULES.md"
    ).read_text()
    assert "второстепенные — от 48×48" in review
    logger.info(
        "LCD_MAP_EXPAND_STATIC=PASS geometry={}x{} gaps={}/{} catalog_bindings=3",
        width,
        height,
        x - (rx + rw),
        card_width - (x + width),
    )


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    main()
