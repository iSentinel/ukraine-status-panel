#!/usr/bin/env python3
"""Generate fast fullscreen map assets from canonical binary geometry."""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "dashboard-map-0.115.44"
GEOMETRY = SOURCE / "geometry.json"
OUTPUT = SOURCE / "fullscreen-fast"
BACKGROUND = (0x07, 0x11, 0x1F)
OUTLINE_COLOR = (0x91, 0xA8, 0xB2)


def main() -> None:
    """Create an opaque base and region masks with outline pixels removed."""
    geometry = json.loads(GEOMETRY.read_text(encoding="utf-8"))
    view = geometry["views"]["fullscreen"]
    outline = Image.open(SOURCE / view["outline"]).convert("L")
    expected_size = (view["width"], view["height"])
    if outline.size != expected_size:
        raise RuntimeError(
            f"fullscreen outline size {outline.size} != {expected_size}"
        )

    OUTPUT.mkdir(parents=True, exist_ok=True)
    masks_out = OUTPUT / "masks-binary"
    masks_out.mkdir(parents=True, exist_ok=True)

    base = Image.new("RGB", outline.size, BACKGROUND)
    base_pixels = base.load()
    outline_pixels = outline.load()
    for y in range(outline.height):
        for x in range(outline.width):
            if outline_pixels[x, y]:
                base_pixels[x, y] = OUTLINE_COLOR
    base.save(OUTPUT / "base-rgb565.png", optimize=True)

    source_masks = SOURCE / view["mask_dir"]
    total_removed = 0
    for iso, meta in view["regions"].items():
        filename = f"region-{iso.lower()}.png"
        mask = Image.open(source_masks / filename).convert("L")
        expected = (meta["width"], meta["height"])
        if mask.size != expected:
            raise RuntimeError(f"{filename}: size {mask.size} != {expected}")
        pixels = mask.load()
        removed = 0
        for yy in range(mask.height):
            canvas_y = meta["y"] + yy
            for xx in range(mask.width):
                if not pixels[xx, yy]:
                    continue
                canvas_x = meta["x"] + xx
                if outline_pixels[canvas_x, canvas_y]:
                    pixels[xx, yy] = 0
                    removed += 1
        total_removed += removed
        mask.save(masks_out / filename, optimize=True)

    metadata = {
        "source": str(SOURCE.relative_to(ROOT)),
        "base": "base-rgb565.png",
        "mask_dir": "masks-binary",
        "width": outline.width,
        "height": outline.height,
        "background": "#07111F",
        "outline": "#91A8B2",
        "outline_pixels_removed_from_regions": total_removed,
        "region_count": len(view["regions"]),
    }
    (OUTPUT / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "FULLSCREEN_FAST_ASSETS=PASS "
        f"regions={len(view['regions'])} removed={total_removed}"
    )


if __name__ == "__main__":
    main()
