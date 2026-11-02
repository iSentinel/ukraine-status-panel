#!/usr/bin/env python3
"""Measure hourly weather wrapping with actual generated fonts and real LVGL."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
from types import SimpleNamespace

from loguru import logger
import yaml

ROOT = Path(__file__).resolve().parents[1]
DUMP = '#include "usp_ui_i18n_0_115_107.h"\n#include <cstdio>\n#include <cstring>\nint main() {\n  for (const auto &item : usp_weather_codes::DEFINITIONS) {\n    for (unsigned lang = 0; lang < 3; ++lang) {\n      const auto text = usp_ui_i18n::compact_weather_condition(\n          usp_weather_codes::description(item.code, lang), lang);\n      if (text != item.compact[lang]) return 2;\n      std::printf("%u\\t%u\\t%s\\n", item.code, lang, text.c_str());\n    }\n  }\n}\n'


def load(name, path):
    """Reuse project parsers and tool discovery without reading secrets."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def arguments():
    """Require fresh evidence and explicitly selected generated font source."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--source-yaml", type=Path, default=ROOT / "firmware/ukraine-status-panel.yaml"
    )
    parser.add_argument(
        "--lvgl-root", type=Path, default=os.environ.get("USP_LVGL_ROOT")
    )
    args = parser.parse_args()
    if args.lvgl_root is None or not (args.lvgl_root / "lvgl.h").is_file():
        parser.error("Set USP_LVGL_ROOT to the actual LVGL library")
    return args


def config_rows(path):
    """Read actual widget positions without resolving private YAML tags."""
    layout = load("weather_layout", ROOT / "tests/weather-touch-layout.py")
    reference = load("lcd_reference", ROOT / "tests/lcd-navigation-static.py")
    reference.load_config()
    config = yaml.load(path.read_text(), Loader=reference.ReferenceLoader)
    return {
        row["widget"]["id"]: row
        for page in config["lvgl"][0]["pages"]
        for row in layout.walk(page, page["id"])
        if "id" in row["widget"]
    }


def font_data(main, size):
    """Extract the same line height and glyph metrics used by ESPHome."""
    width = load("weather_width", ROOT / "tests/weather-label-width-generated.py")
    pattern = r"new\(usp_i18n_" + str(size) + r"\) font::Font\(\w+, \d+, (\d+), (\d+),"
    metrics = re.search(pattern, main)
    assert metrics, size
    baseline, height = map(int, metrics.groups())
    return baseline, height, width.glyphs(main, size)


def rectangles(rows):
    """Read six real label rectangles and explicit spacing settings."""
    result = []
    for index in range(6):
        item = rows[f"weather_hour_{index}_condition"]["widget"]
        assert item["text_font"] == "usp_i18n_18" and item["long_mode"] == "WRAP"
        result.append(
            "{"
            + ",".join(
                map(
                    str,
                    [
                        index,
                        item["width"],
                        item["height"],
                        item.get("text_letter_space", 0),
                        item.get("text_line_space", 0),
                    ],
                )
            )
            + "}"
        )
    return result


def fixture(args):
    """Bind all descriptions and six label sizes to generated font metrics."""
    width = load("weather_width", ROOT / "tests/weather-label-width-generated.py")
    rows = config_rows(args.source_yaml)
    main = args.main_cpp.read_text()
    baseline, height, values = font_data(main, 18)
    glyphs = [
        "{" + ",".join(map(str, [code] + value)) + "}" for code, value in values.items()
    ]
    samples = [
        "{" + code + "," + language + "," + json.dumps(text, ensure_ascii=False) + "}"
        for code, language, text in width.labels(DUMP)
    ]
    header = (
        "#pragma once\nstruct Glyph {unsigned code; int advance,x,y,w,h;};\n"
        "struct Sample {unsigned code,language; const char *text;};\n"
        "struct Rect {int card,width,height,letter_space,line_space;};\n"
        f"constexpr int FONT_HEIGHT={height}, FONT_BASELINE={height-baseline};\n"
        + "constexpr Glyph GLYPHS[]={"
        + ",".join(glyphs)
        + "};\n"
        + "constexpr Sample SAMPLES[]={"
        + ",".join(samples)
        + "};\n"
        + "constexpr Rect RECTS[]={"
        + ",".join(rectangles(rows))
        + "};\n"
    )
    (args.build_dir / "weather-wrap-fixture.h").write_text(header)


def build(args):
    """Compile real LVGL with memory checks; never contact the panel."""
    gate = load("lvgl_gate", ROOT / "tests/lcd-navigation-lvgl-native.py")
    options = SimpleNamespace(
        lvgl_root=args.lvgl_root, controller_include=ROOT / "include", sanitize=True
    )
    cmake = gate.tool_path("cmake", None)
    ninja = gate.tool_path("ninja", None)
    command = gate.configure_command(options, args.build_dir / "build", cmake, ninja)
    command.append(f"-DUSP_WEATHER_WRAP_FIXTURE_DIR={args.build_dir.resolve()}")
    gate.run_command(command, args.build_dir / "configure.log")
    gate.run_command(
        [
            cmake,
            "--build",
            str(args.build_dir / "build"),
            "--target",
            "weather_wrap",
            "-j2",
        ],
        args.build_dir / "build.log",
    )


def measure(args):
    """Keep every measured description, including failures, for review."""
    with (args.build_dir / "measurements.tsv").open("w") as output:
        result = subprocess.run(
            [str(args.build_dir / "build/weather_wrap")],
            stdout=output,
            stderr=subprocess.PIPE,
            text=True,
        )
    lines = (args.build_dir / "measurements.tsv").read_text().splitlines()
    samples = [line.split("\t", 6) for line in lines]
    report = dict(
        status="PASS" if result.returncode == 0 else "FAIL",
        measurements=len(samples),
        generated_main_sha256=hashlib.sha256(args.main_cpp.read_bytes()).hexdigest(),
        source_yaml_sha256=hashlib.sha256(args.source_yaml.read_bytes()).hexdigest(),
        codes_header_sha256=hashlib.sha256(
            (ROOT / "include/usp_weather_codes.h").read_bytes()
        ).hexdigest(),
        physical_visual_acceptance=False,
        overflow=[row for row in samples if int(row[4]) > int(row[5])],
        maximum_height=max(int(row[4]) for row in samples),
    )
    (args.build_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    )
    (args.build_dir / "result.log").write_text(result.stderr)
    logger.info("{}", result.stderr.strip())
    assert (
        len(samples) == 522 and result.returncode == 0
    ), "Hourly descriptions do not fit"


def row_samples():
    """Include complete localized rain labels and all displayed time values."""
    rain = [
        prefix + f"{n}%"
        for prefix in ("Опади: ", "Осадки: ", "Precip: ")
        for n in range(101)
    ]
    return (
        ("time", [f"{h:02}:{m:02}" for h in range(24) for m in range(60)]),
        ("temp", [f"{n}°C" for n in range(-200, 201)]),
        ("rain", rain),
    )


def check_layout(args):
    """Keep painted text inside each card and away from adjacent rows."""
    width = load("weather_width", ROOT / "tests/weather-label-width-generated.py")
    geometry = load("weather_layout", ROOT / "tests/weather-touch-layout.py")
    rows = config_rows(args.source_yaml)
    main = args.main_cpp.read_text()
    values = {size: font_data(main, size) for size in (18, 20, 26)}
    for index in range(6):
        prefix = f"weather_hour_{index}_"
        names = ("time", "temp", "condition", "rain")
        rectangles = []
        for name in names:
            row = rows[prefix + name]
            geometry.contained(row)
            rectangles.append(geometry.rect(row))
        for offset, first in enumerate(rectangles):
            assert not any(
                geometry.overlap(first, other) for other in rectangles[offset + 1 :]
            )
        for name, samples in row_samples():
            item = rows[prefix + name]["widget"]
            size = int(item["text_font"].rsplit("_", 1)[1])
            _, height, glyphs = values[size]
            assert height <= item["height"], (prefix, name, height)
            assert max(width.text_width(s, glyphs) for s in samples) <= item["width"]
        icon = rows[prefix + "icon"]
        drops = rows[prefix + "drops"]
        icon_area = (
            icon["x"],
            icon["y"],
            icon["widget"]["width"],
            drops["y"] + drops["widget"]["height"] - icon["y"],
        )
        assert not any(geometry.overlap(icon_area, rect) for rect in rectangles)
        for suffix in ("icon", "drops", "moon_outer", "moon_inner"):
            geometry.contained(rows[prefix + suffix])


def main():
    """Preserve failing evidence before any layout changes."""
    args = arguments()
    args.build_dir.mkdir(exist_ok=False)
    fixture(args)
    build(args)
    measure(args)
    check_layout(args)


if __name__ == "__main__":
    main()
