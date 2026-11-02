#!/usr/bin/env python3
"""Measure power button labels using actual generated ESPHome glyphs."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    """Reuse the project's geometry and generated font readers."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mappings(value):
    """Visit nested action dictionaries without executing any action."""
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from mappings(child)
    elif isinstance(value, list):
        for child in value:
            yield from mappings(child)


def translations(source, layout):
    """Compile the real refresh text lambda instead of copying translations."""
    actions = [
        node["lvgl.button.update"]
        for node in mappings(layout.config(source))
        if isinstance(node.get("lvgl.button.update"), dict)
        and node["lvgl.button.update"].get("id") == layout.IDS[2]
    ]
    assert len(actions) == 1
    body = actions[0]["text"].replace("id(ui_language)", "language")
    program = (
        "#include <string>\n#include <cstdio>\n"
        "std::string refresh(unsigned language) {\n" + body + "\n}\n"
        "int main() { for(unsigned n=0;n<3;++n) "
        'std::printf("%u\\t%s\\n",n,refresh(n).c_str()); }\n'
    )
    with TemporaryDirectory(prefix="usp-power-text-") as temporary:
        directory = Path(temporary)
        cpp, binary = directory / "labels.cpp", directory / "labels"
        cpp.write_text(program)
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(cpp),
                "-o",
                str(binary),
            ],
            check=True,
        )
        output = subprocess.check_output([str(binary)], text=True)
    return [line.split("\t", 1) for line in output.splitlines()]


def measurements(args):
    """Check line height and painted width for all three language choices."""
    layout = load("power_layout", ROOT / "tests/power-touch-layout.py")
    font = load("forecast_font", ROOT / "tests/weather-forecast-wrap-generated.py")
    width = load("font_width", ROOT / "tests/weather-label-width-generated.py")
    rows = layout.items(args.source_yaml)
    main = args.main_cpp.read_text()
    samples = [(key, None, rows[key]["widget"]["text"]) for key in layout.IDS[:2]]
    samples += [
        (layout.IDS[2], int(lang), text)
        for lang, text in translations(args.source_yaml, layout)
    ]
    result = []
    for name, language, text in samples:
        widget = rows[name]["widget"]
        size = int(widget["text_font"].rsplit("_", 1)[1])
        _, height, glyphs = font.font_data(main, size)
        painted = width.text_width(text, glyphs)
        assert painted <= widget["width"] and height <= widget["height"], (name, text)
        result.append(
            dict(
                id=name,
                language=language,
                text=text,
                font=size,
                width=painted,
                line_height=height,
                available=[widget["width"], widget["height"]],
            )
        )
    assert len(result) == 5
    return result


def main():
    """Bind width evidence to the actual generated source and canonical YAML."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument(
        "--source-yaml", type=Path, default=ROOT / "firmware/ukraine-status-panel.yaml"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Evidence exists; use a new output path")
    samples = measurements(args)
    report = dict(
        status="PASS",
        measurements=len(samples),
        samples=samples,
        generated_main_sha256=hashlib.sha256(args.main_cpp.read_bytes()).hexdigest(),
        source_yaml_sha256=hashlib.sha256(args.source_yaml.read_bytes()).hexdigest(),
        physical_touch_tested=False,
        device_contact=False,
    )
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    logger.info("POWER_TOUCH_FONT=PASS measurements=5 actual refresh lambda UA/RU/EN")


if __name__ == "__main__":
    main()
