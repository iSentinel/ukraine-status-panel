#!/usr/bin/env python3
"""Generate final visible map spans without changing its source geometry."""

import argparse
import hashlib
import json
from pathlib import Path

from loguru import logger
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/dashboard-map-0.115.44"


def labels():
    """Apply regions in the existing drawing order, then the outline."""
    geometry = json.loads((SOURCE / "geometry.json").read_text())
    view = geometry["views"]["fullscreen"]
    canvas = Image.new("L", (view["width"], view["height"]), 0)
    for index, (iso, box) in enumerate(view["regions"].items(), 1):
        path = (
            SOURCE
            / "fullscreen-fast/masks-binary"
            / f"region-{iso.lower()}.png"
        )
        mask = (
            Image.open(path)
            .convert("L")
            .point(lambda value: 255 if value else 0)
        )
        canvas.paste(index, (box["x"], box["y"]), mask)
    mask = Image.open(SOURCE / view["outline"]).convert("L")
    canvas.paste(26, (0, 0), mask.point(lambda value: 255 if value else 0))
    return canvas, list(view["regions"])


def spans(canvas):
    """Group horizontal visible spans by their final color owner."""
    groups = [[] for _ in range(27)]
    pixels = canvas.tobytes()
    for y in range(canvas.height):
        row = pixels[y * canvas.width : (y + 1) * canvas.width]
        start = 0
        while start < len(row):
            end = start + 1
            while end < len(row) and row[end] == row[start]:
                end += 1
            if row[start]:
                groups[row[start]].append(
                    (y * canvas.width + start, end - start)
                )
            start = end
    return groups


def header(canvas, groups):
    """Write portable C++ data; no packed or unaligned memory access."""
    starts, records = [], []
    for group in groups:
        starts.append(len(records))
        records.extend(group)
    starts.append(len(records))
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "namespace usp_map_cache_data {",
        "struct Run { uint32_t offset; uint16_t length; };",
        f"constexpr uint16_t WIDTH = {canvas.width}, HEIGHT = {canvas.height};",
        "constexpr uint32_t STARTS[28] = {"
        + ",".join(map(str, starts))
        + "};",
        "constexpr Run RUNS[] = {",
    ]
    lines.extend(f"  {{{offset}, {length}}}," for offset, length in records)
    lines.extend(["};", "}  // namespace usp_map_cache_data", ""])
    return "\n".join(lines), len(records)


def main():
    """Generate an asset or check that the saved one remains reproducible."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    canvas, regions = labels()
    content, count = header(canvas, spans(canvas))
    target = ROOT / "include/usp_fullscreen_map_runs.h"
    if args.check:
        if not target.exists() or target.read_text() != content:
            raise RuntimeError("Regenerate fullscreen map cache spans")
    else:
        target.write_text(content)
    logger.info(
        "Map spans: {} regions={} buffer_bytes={} source_sha256={}",
        count,
        len(regions),
        canvas.width * canvas.height * 2,
        hashlib.sha256(canvas.tobytes()).hexdigest(),
    )


if __name__ == "__main__":
    main()
