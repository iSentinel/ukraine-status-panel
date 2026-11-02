#!/usr/bin/env python3
"""Measure Home header text with actual generated fonts, including all times."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def samples(name):
    """Exercise date/time boundaries and every source status caption."""
    if name == "header_clock_label":
        yield "--:--:--"
        for second in range(86400):
            yield f"{second // 3600:02}:{second // 60 % 60:02}:{second % 60:02}"
    elif name == "header_date_label":
        yield "--.--.----"
        for year in range(2000, 2100):
            for month in range(1, 13):
                for day in range(1, 32):
                    yield f"{day:02}.{month:02}.{year:04}"
    elif name == "dashboard_header_mains_caption":
        header = (ROOT / "include/usp_home_page_text.h").read_text()
        table = header.split("LABELS[3][25]", 1)[1].split("};", 1)[0]
        values = re.findall(r'"([^"]*)"', table)
        assert len(values) == 75
        yield from (values[i * 25 + 24] for i in range(3))
    elif name == "dashboard_top_grid_label":
        text = (ROOT / "include/usp_ui_i18n_0_115_107.h").read_text()
        part = text.split("compact_energy_status(", 1)[1].split("\n}", 1)[0]
        yield from re.findall(r'"([^"]*)"', part)
        yield from (f"#{value}" for value in range(256))
    elif name == "dashboard_language_label":
        yield from ("UA", "RU", "EN")
    elif name == "dashboard_brand_label":
        yield "Ukraine Status Panel"


def measure(name, row, main, reader):
    """Require full text and one complete line inside the content rectangle."""
    font = row["text_font"]
    glyphs = reader.glyphs(main, font)
    pattern = (
        r"new\(" + re.escape(font) + r"\) font::Font\(\w+, \d+, \d+, (\d+),"
    )
    height = int(re.search(pattern, main)[1])
    available = (
        row["height"] - row.get("pad_top", 0) - row.get("pad_bottom", 0)
    )
    assert height <= available, (name, height, available)
    maximum, worst, count = 0, "", 0
    for text in samples(name):
        width = reader.text_width(text, glyphs)
        assert width <= row["width"], (name, text, width, row["width"])
        count += 1
        if width > maximum:
            maximum, worst = width, text
    return dict(
        id=name,
        samples=count,
        maximum_width=maximum,
        worst=worst,
        available_width=row["width"],
        line_height=height,
        available_height=available,
    )


def main():
    """Write a fresh software-only receipt bound to generated main.cpp."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "home_layout", ROOT / "tests/home-header-layout.py"
    )
    layout = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(layout)
    reader = layout.load(
        "glyphs", ROOT / "tests/settings-touch-width-generated.py"
    )
    raw = args.main_cpp.read_bytes()
    main_text = raw.decode()
    rows = layout.verify()
    result = dict(
        status="PASS",
        measurements=[
            measure(name, row, main_text, reader) for name, row in rows.items()
        ],
        main_sha256=hashlib.sha256(raw).hexdigest(),
        physical_visual_acceptance=False,
        device_contact=False,
    )
    with args.output.open("x") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    logger.info("HOME_HEADER_FONT=PASS slots7 exhaustive_clock86400")


if __name__ == "__main__":
    main()
