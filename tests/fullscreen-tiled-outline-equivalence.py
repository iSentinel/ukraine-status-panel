#!/usr/bin/env python3
"""Prove tiled fullscreen composition is RGB565-equivalent to legacy output."""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "dashboard-map-0.115.44"
FAST = SOURCE / "fullscreen-fast"
TILED = SOURCE / "fullscreen-tiled"
GEOMETRY = json.loads((SOURCE / "geometry.json").read_text(encoding="utf-8"))
VIEW = GEOMETRY["views"]["fullscreen"]
META = json.loads((TILED / "metadata.json").read_text(encoding="utf-8"))
BACKGROUND = 0x07111F
OUTLINE = 0x91A8B2
PALETTES = {
    0: {0: 0x34424E, 1: 0x1F8A4C, 2: 0xF5BE26, 16: 0xF97316, 4: 0xDC3545, 32: 0x9C27B0},
    1: {0: 0x56616D, 1: 0x0072B2, 2: 0xF0E442, 16: 0xE69F00, 4: 0xD55E00, 32: 0xCC79A7},
    2: {0: 0x4D4D4D, 1: 0xA6A6A6, 2: 0xD9D9D9, 16: 0xBFBFBF, 4: 0xF2F2F2, 32: 0x737373},
}
STATES = (0, 1, 2, 16, 4, 32)


def rgb(value: int) -> tuple[int, int, int]:
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def quantize_rgb565(image: Image.Image) -> Image.Image:
    src = image.convert("RGB")
    out = Image.new("RGB", src.size)
    data = []
    for r, g, b in src.getdata():
        r5, g6, b5 = r >> 3, g >> 2, b >> 3
        data.append((r5 * 255 // 31, g6 * 255 // 63, b5 * 255 // 31))
    out.putdata(data)
    return out


def paint(canvas: Image.Image, mask: Image.Image, xy: tuple[int, int], color: int) -> None:
    binary = mask.convert("L").point(lambda p: 255 if p else 0)
    fill = Image.new("RGB", binary.size, rgb(color))
    canvas.paste(fill, xy, binary)


def legacy(states: list[int], palette: int) -> Image.Image:
    canvas = Image.new("RGB", (VIEW["width"], VIEW["height"]), rgb(BACKGROUND))
    mask_dir = SOURCE / VIEW["mask_dir"]
    for (iso, meta), state in zip(VIEW["regions"].items(), states):
        mask = Image.open(mask_dir / f"region-{iso.lower()}.png")
        paint(canvas, mask, (meta["x"], meta["y"]), PALETTES[palette][state])
    paint(canvas, Image.open(SOURCE / VIEW["outline"]), (0, 0), OUTLINE)
    return quantize_rgb565(canvas)


def tiled(states: list[int], palette: int) -> Image.Image:
    canvas = Image.new("RGB", (VIEW["width"], VIEW["height"]), rgb(BACKGROUND))
    mask_dir = FAST / "masks-binary"
    for (iso, meta), state in zip(VIEW["regions"].items(), states):
        mask = Image.open(mask_dir / f"region-{iso.lower()}.png")
        paint(canvas, mask, (meta["x"], meta["y"]), PALETTES[palette][state])
    for rec in META["tiles"]:
        paint(
            canvas,
            Image.open(TILED / "outline-tiles" / rec["file"]),
            (rec["x"], rec["y"]),
            OUTLINE,
        )
    return quantize_rgb565(canvas)


def diff_pixels(left: Image.Image, right: Image.Image) -> int:
    diff = ImageChops.difference(left, right)
    return sum(1 for pixel in diff.getdata() if pixel != (0, 0, 0))


def main() -> None:
    region_count = len(VIEW["regions"])
    cases = [(f"all-{state}", [state] * region_count) for state in STATES]
    cases += [
        ("mixed", [STATES[i % len(STATES)] for i in range(region_count)]),
        ("mixed-reverse", [STATES[-1 - (i % len(STATES))] for i in range(region_count)]),
    ]
    checked = 0
    for palette in PALETTES:
        for label, states in cases:
            changed = diff_pixels(legacy(states, palette), tiled(states, palette))
            if changed:
                raise RuntimeError(
                    f"tiled fullscreen differs: palette={palette} case={label} pixels={changed}"
                )
            checked += 1
    if META["reconstruction_diff_pixels"] != 0:
        raise RuntimeError("outline tile reconstruction is not exact")
    print(
        "FULLSCREEN_TILED_EQUIVALENCE=PASS "
        f"cases={checked} pixels_diff=0 tiles={META['tile_count']}"
    )


if __name__ == "__main__":
    main()
