#!/usr/bin/env python3
"""Measure new Settings captions with actual generated Noto Sans glyphs."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    """Import the existing source and glyph readers."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def strings(header, marker):
    """Read literal captions from the actual production C++ table."""
    body = header.split(marker, 1)[1].split("};", 1)[0]
    return [
        json.loads(value) for value in re.findall(r'"(?:\\.|[^"\\])*"', body)
    ]


def wrapped_lines(text, width, glyphs, measure):
    """Count whole-word lines using the generated glyph advances."""
    lines = 1
    current = ""
    for word in text.split():
        assert measure(word, glyphs) <= width, word
        combined = (current + " " + word).strip()
        if current and measure(combined, glyphs) > width:
            lines += 1
            current = word
        else:
            current = combined
    return lines


def captions(main, header, rows, reader):
    """Check every sidebar caption in UA/RU/EN with its configured font."""
    words = strings(header, "constexpr const char *NAMES")
    names = ("display", "language", "region", "sound", "network", "system")
    assert len(words) == 18
    glyphs = reader.glyphs(main, "usp_i18n_20")
    result = []
    for index, text in enumerate(words):
        name = "settings_section_" + names[index % 6]
        widget = rows[name]["widget"]
        assert widget["text_font"] == "usp_i18n_20"
        actual = reader.text_width(text, glyphs)
        assert actual <= widget["width"] - 16, (name, text, actual)
        result.append(
            dict(
                id=name,
                language=index // 6,
                text=text,
                width=actual,
                limit=widget["width"] - 16,
            )
        )
    return result


def notes(main, header, rows, reader):
    """Check coverage and wrapped height for each explanatory paragraph."""
    words = strings(header, "constexpr const char *NOTES")
    names = ("language", "region", "sound", "network")
    assert len(words) == 12
    glyphs = reader.glyphs(main)
    match = re.search(
        r"new\(usp_i18n_18\) font::Font\(\w+, \d+, \d+, (\d+),", main
    )
    assert match
    line_height = int(match.group(1))
    result = []
    for index, text in enumerate(words):
        name = "settings_" + names[index % 4] + "_note"
        widget = rows[name]["widget"]
        lines = wrapped_lines(text, widget["width"], glyphs, reader.text_width)
        assert lines * line_height <= widget["height"], (name, text, lines)
        result.append(
            dict(
                id=name,
                language=index // 4,
                lines=lines,
                line_height=line_height,
                height=widget["height"],
            )
        )
    return result


def main():
    """Create new evidence; never contact the panel or overwrite a report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    layout = load("settings_layout", ROOT / "tests/settings-touch-layout.py")
    reader = load(
        "settings_width", ROOT / "tests/settings-touch-width-generated.py"
    )
    source = args.main_cpp.read_text()
    header = (ROOT / "include/usp_settings_sections.h").read_text()
    rows = layout.items(ROOT / "firmware/ukraine-status-panel.yaml")
    result = dict(
        status="PASS",
        captions=captions(source, header, rows, reader),
        notes=notes(source, header, rows, reader),
        physical_visual_acceptance=False,
    )
    with args.output.open("x") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    logger.info("SETTINGS_SECTIONS_WIDTH=PASS captions18 notes12 UA/RU/EN")


if __name__ == "__main__":
    main()
