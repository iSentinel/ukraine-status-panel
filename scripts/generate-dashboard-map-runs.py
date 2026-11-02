#!/usr/bin/env python3
"""Generate last-owner RGB565 run geometry for the Home dashboard map."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "firmware/assets/dashboard-map-0.115.44"
GEOMETRY = ASSETS / "geometry.json"
DEFAULT_OUTPUT = ROOT / "firmware/include/usp_dashboard_map_runs.h"


def load_view() -> dict:
    """Return the checked-in dashboard geometry."""
    data = json.loads(GEOMETRY.read_text(encoding="utf-8"))
    return data["views"]["dashboard"]


def paint_owners(view: dict) -> tuple[bytearray, int]:
    """Composite masks in LVGL widget order and return final pixel owners."""
    width, height = view["width"], view["height"]
    owners = bytearray(width * height)
    overlaps = 0
    mask_dir = ASSETS / view["mask_dir"]
    for label, (iso, meta) in enumerate(view["regions"].items(), 1):
        mask = Image.open(mask_dir / f"region-{iso.lower()}.png").convert("L")
        if mask.size != (meta["width"], meta["height"]):
            raise RuntimeError(f"geometry mismatch for {iso}: {mask.size}")
        overlaps += paint_mask(owners, width, mask, meta["x"], meta["y"], label)
    outline = Image.open(ASSETS / view["outline"]).convert("L")
    if outline.size != (width, height):
        raise RuntimeError(f"outline mismatch: {outline.size}")
    paint_mask(owners, width, outline, 0, 0, 26)
    return owners, overlaps


def paint_mask(
    owners: bytearray,
    width: int,
    mask: Image.Image,
    left: int,
    top: int,
    label: int,
) -> int:
    """Paint one binary mask and return overwritten nonzero pixel count."""
    overlaps = 0
    for y in range(mask.height):
        row = (top + y) * width + left
        for x in range(mask.width):
            if mask.getpixel((x, y)) == 0:
                continue
            offset = row + x
            overlaps += owners[offset] != 0
            owners[offset] = label
    return overlaps


def runs_for_label(owners: bytearray, label: int) -> list[tuple[int, int]]:
    """Return contiguous linear runs owned by one label."""
    runs: list[tuple[int, int]] = []
    start = -1
    for offset, owner in enumerate(owners):
        if owner == label and start < 0:
            start = offset
        if owner != label and start >= 0:
            runs.append((start, offset - start))
            start = -1
    if start >= 0:
        runs.append((start, len(owners) - start))
    return runs


def collect_runs(owners: bytearray) -> tuple[list[int], list[tuple[int, int]]]:
    """Collect region and outline runs with sentinel offsets."""
    starts = [0]
    all_runs: list[tuple[int, int]] = []
    for label in range(1, 27):
        starts.append(len(all_runs))
        all_runs.extend(runs_for_label(owners, label))
    starts.append(len(all_runs))
    return starts, all_runs


def render_header(view: dict, owners: bytearray) -> str:
    """Render deterministic C++ constants for the native cache renderer."""
    starts, all_runs = collect_runs(owners)
    if any(length > 0xFFFF for _offset, length in all_runs):
        raise RuntimeError("run length exceeds uint16_t")
    start_text = ",".join(str(value) for value in starts)
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "namespace usp_dashboard_map_cache_data {",
        "struct Run { uint32_t offset; uint16_t length; };",
        f"constexpr uint16_t WIDTH = {view['width']}, HEIGHT = {view['height']};",
        f"constexpr uint32_t STARTS[28] = {{{start_text}}};",
        "constexpr Run RUNS[] = {",
    ]
    lines.extend(f"  {{{offset}, {length}}}," for offset, length in all_runs)
    lines.extend(["};", "}  // namespace usp_dashboard_map_cache_data", ""])
    return "\n".join(lines)


def main() -> None:
    """Write or verify the generated dashboard run header."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    view = load_view()
    owners, overlaps = paint_owners(view)
    content = render_header(view, owners)
    if args.check:
        if not args.output.exists() or args.output.read_text() != content:
            raise SystemExit("DASHBOARD_MAP_RUNS=FAIL generated header is stale")
    else:
        args.output.write_text(content)
    _starts, runs = collect_runs(owners)
    covered = sum(value != 0 for value in owners)
    print(
        "DASHBOARD_MAP_RUNS=PASS "
        f"size={view['width']}x{view['height']} overlaps={overlaps} "
        f"covered={covered} runs={len(runs)}"
    )


if __name__ == "__main__":
    main()
