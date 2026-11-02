#!/usr/bin/env python3
"""Measure all eight navigation captions with the generated LCD font."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
NAMES = (
    "home",
    "map",
    "weather",
    "power",
    "history",
    "spotify",
    "clock",
    "settings",
)


def load(name, path):
    """Reuse the project's inert YAML and generated-glyph readers."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def controls(value):
    """Find the actual positioned navigation widgets, not action references."""
    result = {}
    if isinstance(value, dict):
        if (
            str(value.get("id", "")).startswith("lcd_nav_")
            and "width" in value
        ):
            result[value["id"]] = value
        for child in value.values():
            result.update(controls(child))
    elif isinstance(value, list):
        for child in value:
            result.update(controls(child))
    return result


def measure(config, main, reader):
    """Check source catalog captions against the source widget widths."""
    header = (ROOT / "include/usp_navigation_text.h").read_text()
    table = header.split("LABELS[3][8]", 1)[1].split("};", 1)[0]
    captions = [json.loads(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', table)]
    assert len(captions) == 24
    widgets = controls(config)
    glyphs = reader.glyphs(main)
    result = []
    for language in range(3):
        for index, name in enumerate(NAMES):
            widget = widgets["lcd_nav_" + name]
            assert widget["text_font"] == "usp_i18n_18"
            assert widget["pad_all"] == 0
            text = captions[language * 8 + index]
            width = reader.text_width(text, glyphs)
            assert width <= widget["width"], (language, name, text, width)
            result.append(
                dict(
                    language=language,
                    id=widget["id"],
                    text=text,
                    width=width,
                    limit=widget["width"],
                )
            )
    return result


def main():
    """Save new software evidence without contacting the device."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    layout = load("settings_layout", ROOT / "tests/settings-touch-layout.py")
    reader = load(
        "settings_width", ROOT / "tests/settings-touch-width-generated.py"
    )
    raw = args.main_cpp.read_bytes()
    result = dict(
        status="PASS",
        captions=measure(
            layout.config(ROOT / "firmware/ukraine-status-panel.yaml"),
            raw.decode(),
            reader,
        ),
        generated_main_sha256=hashlib.sha256(raw).hexdigest(),
        physical_visual_acceptance=False,
    )
    with args.output.open("x") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    logger.info("NAVIGATION_WIDTH=PASS captions24 languages3 actual_font18")


if __name__ == "__main__":
    main()
