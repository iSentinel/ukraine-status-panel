#!/usr/bin/env python3
"""Measure dynamic Settings labels with actual generated Noto Sans glyphs."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = {
    "web_password_reset_ui_button": (
        "НАТИСНІТЬ ЩЕ РАЗ", "НАЖМИТЕ ЕЩЁ РАЗ", "PRESS AGAIN TO CONFIRM",
        "СКИНУТИ WEB-ПАРОЛЬ", "СБРОС WEB-ПАРОЛЯ", "RESET WEB PASSWORD",
    ),
    "settings_sound_toggle_button": (
        "ЗВУК: УВІМК", "ЗВУК: ВИМК", "ЗВУК: ВКЛ", "ЗВУК: ВЫКЛ",
        "SOUND: ON", "SOUND: OFF",
    ),
    "settings_buzzer_test_button": ("ТЕСТ БУЗЕРА", "ТЕСТ БУЗЗЕРА", "BUZZER TEST"),
    "battery_installed_button": (
        "БАТАРЕЯ: Є", "БАТАРЕЯ: НЕМАЄ",
        "БАТАРЕЯ: ЕСТЬ", "БАТАРЕЯ: НЕТ",
        "BATTERY: INSTALLED", "BATTERY: NOT INSTALLED",
    ),
}


def glyphs(main, font_name="usp_i18n_18"):
    """Read the exact table for this font, including constexpr declarations."""
    match = re.search(r"new\(" + re.escape(font_name) + r"\) font::Font\((\w+),", main)
    assert match, "generated usp_i18n_18 missing"
    marker = "font::Glyph " + match.group(1) + "[]"
    line = next(line for line in main.splitlines() if marker in line)
    pattern = r"\{(\d+), \([^)]*\), (\d+), (-?\d+), (-?\d+), (\d+), (\d+)\}"
    return {int(row[0]): [int(v) for v in row[1:]] for row in re.findall(pattern, line)}


def text_width(text, values):
    advance = right = 0
    for char in text:
        assert ord(char) in values, f"missing glyph {char!r}"
        g = values[ord(char)]
        right = max(right, advance + g[1] + g[3])
        advance += g[0]
    return max(right, advance)


def widget_width(source, name):
    start = source.index(f"id: {name}\n")
    block = "\n".join(source[start:].splitlines()[:18])
    return int(re.search(r"\bwidth: (\d+)", block)[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists(), "evidence already exists"
    raw = args.main_cpp.read_bytes()
    main_cpp = raw.decode()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    values = glyphs(main_cpp)
    assert "usp_sound_controls_text::label" in source
    binding = (
        source
        + (ROOT / "include/usp_sound_controls_text.h").read_text()
        + (ROOT / "include/usp_battery_model.h").read_text()
    )
    rows = []
    for name, texts in SAMPLES.items():
        limit = widget_width(source, name)
        for text in texts:
            assert text in binding, (name, text, "not source-bound")
            width = text_width(text, values)
            assert width <= limit, (name, text, width, limit)
            rows.append({"id": name, "text": text, "width": width, "limit": limit})
    result = {
        "status": "PASS", "measurements": len(rows), "samples": rows,
        "generated_main_sha256": hashlib.sha256(raw).hexdigest(),
        "source_yaml_sha256": hashlib.sha256(source.encode()).hexdigest(),
    }
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(f"SETTINGS_TOUCH_WIDTH=PASS measurements={len(rows)}")

if __name__ == "__main__":
    main()
