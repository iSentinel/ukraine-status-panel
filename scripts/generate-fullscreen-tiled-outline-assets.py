#!/usr/bin/env python3
"""Generate cropped BINARY outline tiles for the fullscreen map."""
from __future__ import annotations

import json
import shutil
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "dashboard-map-0.115.44"
GEOMETRY = SOURCE / "geometry.json"
OUTPUT = SOURCE / "fullscreen-tiled"
TILES = OUTPUT / "outline-tiles"
COLS = 10
ROWS = 6


def main() -> None:
    geometry = json.loads(GEOMETRY.read_text(encoding="utf-8"))
    view = geometry["views"]["fullscreen"]
    outline = Image.open(SOURCE / view["outline"]).convert("L")
    expected = (view["width"], view["height"])
    if outline.size != expected:
        raise RuntimeError(f"outline size {outline.size} != {expected}")

    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    TILES.mkdir(parents=True)

    records = []
    raw_pixels = 0
    tile_no = 0
    for row in range(ROWS):
        y0 = row * outline.height // ROWS
        y1 = (row + 1) * outline.height // ROWS
        for col in range(COLS):
            x0 = col * outline.width // COLS
            x1 = (col + 1) * outline.width // COLS
            cell = outline.crop((x0, y0, x1, y1))
            bbox = cell.getbbox()
            if bbox is None:
                continue
            bx0, by0, bx1, by1 = bbox
            cropped = cell.crop(bbox).point(lambda p: 255 if p else 0)
            filename = f"outline-tile-{tile_no:02d}.png"
            cropped.save(TILES / filename, optimize=True)
            rec = {
                "id": f"fullscreen_outline_tile_{tile_no:02d}",
                "file": filename,
                "grid_col": col,
                "grid_row": row,
                "x": x0 + bx0,
                "y": y0 + by0,
                "width": cropped.width,
                "height": cropped.height,
            }
            rec["raw_pixels"] = cropped.width * cropped.height
            raw_pixels += rec["raw_pixels"]
            records.append(rec)
            tile_no += 1

    reconstructed = Image.new("L", outline.size, 0)
    for rec in records:
        tile = Image.open(TILES / rec["file"]).convert("L")
        reconstructed.paste(tile, (rec["x"], rec["y"]), tile)
    mismatch = sum(
        1 for left, right in zip(outline.getdata(), reconstructed.getdata())
        if bool(left) != bool(right)
    )
    if mismatch:
        raise RuntimeError(f"tiled outline mismatch pixels={mismatch}")

    metadata = {
        "source": str(SOURCE.relative_to(ROOT)),
        "outline_source": view["outline"],
        "width": outline.width,
        "height": outline.height,
        "grid_cols": COLS,
        "grid_rows": ROWS,
        "tile_count": len(records),
        "raw_crop_pixels": raw_pixels,
        "raw_binary_bytes_estimate": (raw_pixels + 7) // 8,
        "outline_pixels": sum(1 for p in outline.getdata() if p),
        "reconstruction_diff_pixels": mismatch,
        "tiles": records,
    }
    (OUTPUT / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "FULLSCREEN_TILED_OUTLINE_GENERATE=PASS "
        f"grid={COLS}x{ROWS} tiles={len(records)} "
        f"raw_binary_estimate={(raw_pixels + 7)//8} diff={mismatch}"
    )


if __name__ == "__main__":
    main()
