#!/usr/bin/env python3
"""Guard LCD punctuation and dropdown glyph coverage."""

import argparse
from pathlib import Path
from loguru import logger


ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
HEADER = ROOT / "firmware/include/usp_alarm_ui.h"
ALARM_CONTROLS = ROOT / "include/usp_alarm_controls_text.h"
FONT_IDS = tuple(
    f"usp_i18n_{size}" for size in (18, 20, 22, 24, 26, 28, 32, 36)
)
REQUIRED = "'’ʼ«»–—•"


def font_block(text: str, font_id: str) -> str:
    """Return one font declaration block by id."""
    marker = f"    id: {font_id}\n"
    start = text.index(marker)
    next_font = text.find("\n  - file:", start + len(marker))
    end = len(text) if next_font < 0 else next_font
    return text[start:end]


def check_fonts(text: str) -> None:
    """Require shared punctuation in every LCD i18n font."""
    for font_id in FONT_IDS:
        block = font_block(text, font_id)
        missing = [char for char in REQUIRED if char not in block]
        assert not missing, f"{font_id} missing {missing}"
    block_24 = font_block(text, "usp_i18n_24")
    assert 'file: "gfonts://Noto Sans Symbols 2"' in block_24
    assert '          - "▼"' in block_24


def check_usage(yaml_text: str, header_text: str, alarm_text: str) -> None:
    """Keep the guard tied to real production strings and dropdowns."""
    for sample in ("НАТИСНІТЬ ЩЕ РАЗ", "Now: —"):
        assert sample in yaml_text
    for sample in ("«Зберегти»", "Звук будильника увімкнено"):
        assert sample in alarm_text
    for key in ("NOTE", "NOTIFY_SOUND_ON"):
        assert f"usp_alarm_controls_text::Label::{key}" in yaml_text, key
    for sample in ("Пн–Пт", "П'ятниця"):
        assert sample in header_text
    assert yaml_text.count('symbol: "▼"') == 6


def main() -> None:
    """Validate source-level glyph coverage."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    yaml_text = YAML.read_text(encoding="utf-8")
    header_text = HEADER.read_text(encoding="utf-8")
    alarm_text = ALARM_CONTROLS.read_text(encoding="utf-8")
    check_fonts(yaml_text)
    check_usage(yaml_text, header_text, alarm_text)
    logger.info(
        "I18N_GLYPH_COVERAGE_STATIC=PASS "
        "fonts=8 punctuation=8 alarm_dropdowns=6 symbol=U+25BC"
    )


if __name__ == "__main__":
    main()
