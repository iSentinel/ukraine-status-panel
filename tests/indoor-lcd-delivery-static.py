#!/usr/bin/env python3
"""Guard typed indoor text/color delivery without changing sensor semantics."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIGS = (
    ROOT / "firmware/ukraine-status-panel.yaml",
    ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml",
    ROOT / "firmware/ukraine-status-panel-production.yaml",
)


def configs():
    """Return isolated config or all production projections."""
    override = os.environ.get("USP_CONFIG")
    return (Path(override),) if override else DEFAULT_CONFIGS


def script_block(text):
    """Return only the indoor refresh script."""
    start = text.index("  - id: refresh_indoor_environment_ui\n")
    end = text.index("  - id: refresh_room_entry_i18n\n", start)
    return text[start:end]


def verify(path):
    """Require one helper call and unchanged model/formatting inputs."""
    text = path.read_text()
    body = script_block(text)
    assert "include/usp_indoor_lcd_delivery.h" in text
    assert body.count("usp_indoor_lcd_delivery::render(") == 1
    assert body.count("id(weather_indoor_label)") == 1
    assert body.count("id(fullscreen_weather_indoor_label)") == 1
    assert body.count("lv_label_set_text(") == 0
    assert body.count("lv_obj_set_style_text_color(") == 0
    for token in ("detected()", "last_read_ok()", "std::isfinite(",
                  "%s%.1f°C · %d%%", "std::lround(", "0x94A3B8",
                  "0xFACC15", "0x4ADE80"):
        assert token in body, token
    print(f"INDOOR_LCD_DELIVERY_STATIC=PASS config={path.name} helper=1 targets=2")


for selected in configs():
    verify(selected)
