#!/usr/bin/env python3
"""Measure Home power line boxes with the actual generated LCD font."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def measurements(main):
    """Require the full generated line height in each source-defined value."""
    spec = importlib.util.spec_from_file_location(
        "power_layout", ROOT / "tests/home-power-text-layout.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = []
    for name, row in module.verify().items():
        pattern = (
            r"new\("
            + re.escape(row["text_font"])
            + r"\) font::Font\(\w+, \d+, \d+, (\d+),"
        )
        height = int(re.search(pattern, main)[1])
        assert height <= row["height"], (name, height, row["height"])
        result.append(
            dict(
                id=name, font_line_height=height, content_height=row["height"]
            )
        )
    return result


def main():
    """Save new evidence bound to the exact generated main.cpp."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    raw = args.main_cpp.read_bytes()
    report = dict(
        status="PASS",
        measurements=measurements(raw.decode()),
        main_sha256=hashlib.sha256(raw).hexdigest(),
        device_contact=False,
        physical_visual_acceptance=False,
    )
    with args.output.open("x") as stream:
        json.dump(report, stream, indent=2)
    logger.info("HOME_POWER_FONT=PASS values4 actual_font18")


if __name__ == "__main__":
    main()
