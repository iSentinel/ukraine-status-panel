#!/usr/bin/env python3
"""Compare current scaled map masks with a canonical full-canvas resize."""
from __future__ import annotations

import json
import re
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/alert-map-0.115.0'
CURRENT = ROOT / 'assets/dashboard-map-0.115.44'
YAML = ROOT / 'firmware' / 'ukraine-status-panel.yaml'
SCALES = {'home': 0.82, 'full': 1.48}
ASSET_DIR = {
    'home': CURRENT / 'masks-binary',
    'full': CURRENT / 'fullscreen-masks-binary',
}
WIDGET_PREFIX = {'home': 'dash_region_ua_', 'full': 'full_map_region_ua_'}
OUTLINE_ID = {'home': 'dashboard_map_outline', 'full': 'fullscreen_map_origin'}


def parse_xy(text: str, widget_id: str) -> tuple[int, int]:
    pattern = re.compile(
        rf'id: {re.escape(widget_id)}\n(?:\s+[^\n]+\n)*?\s+x: (-?\d+)\n\s+y: (-?\d+)',
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        raise RuntimeError(f'Cannot find x/y for {widget_id}')
    return int(match.group(1)), int(match.group(2))


def binary(image: Image.Image) -> Image.Image:
    return image.convert('L').point(lambda p: 255 if p else 0)


def count_nonzero(image: Image.Image) -> int:
    return sum(1 for px in image.getdata() if px)


def canonical_region(region: dict, scale: float) -> tuple[Image.Image, tuple[int, int, int, int], Image.Image]:
    width, height = meta['width'], meta['height']
    canvas = Image.new('L', (width, height), 0)
    source_mask = binary(Image.open(SOURCE / 'masks-binary' / f"region-{region['iso'].lower()}.png"))
    canvas.paste(source_mask, (region['x'], region['y']))
    target = (round(width * scale), round(height * scale))
    scaled = canvas.resize(target, Image.Resampling.NEAREST)
    bbox = scaled.getbbox()
    if bbox is None:
        raise RuntimeError(f"Empty canonical mask {region['iso']}")
    return scaled, bbox, scaled.crop(bbox)


def canvas_from_current(mode: str, region: dict, origin: tuple[int, int], target: tuple[int, int], widget_xy: tuple[int, int]) -> Image.Image:
    code = region['iso'].split('-')[1]
    asset = binary(Image.open(ASSET_DIR[mode] / f'region-ua-{code}.png'))
    rel = (widget_xy[0] - origin[0], widget_xy[1] - origin[1])
    canvas = Image.new('L', target, 0)
    canvas.paste(asset, rel)
    return canvas


text = YAML.read_text(encoding='utf-8')
meta = json.loads((SOURCE / 'regions.json').read_text(encoding='utf-8'))
regions = meta['regions']
rows = []

for mode, scale in SCALES.items():
    origin = parse_xy(text, OUTLINE_ID[mode])
    target = (round(meta['width'] * scale), round(meta['height'] * scale))
    source_outline = binary(Image.open(SOURCE / 'alerts-map-outline.png')).resize(target, Image.Resampling.NEAREST)
    current_outline_file = CURRENT / ('alerts-map-outline.png' if mode == 'home' else 'fullscreen-alerts-map-outline.png')
    outline_diff = count_nonzero(ImageChops.logical_xor(source_outline.convert('1'), binary(Image.open(current_outline_file)).convert('1')))
    print(f'{mode}: target={target} origin={origin} outline_xor={outline_diff}')
    if outline_diff != 0:
        raise RuntimeError(f'{mode} outline is not canonical')

    total_xor = 0
    bad = 0
    for region in regions:
        code = region['iso'].split('-')[1]
        widget = f"{WIDGET_PREFIX[mode]}{code}"
        xy = parse_xy(text, widget)
        canonical_full, bbox, canonical_crop = canonical_region(region, scale)
        current_full = canvas_from_current(mode, region, origin, target, xy)
        xor = count_nonzero(ImageChops.logical_xor(canonical_full.convert('1'), current_full.convert('1')))
        canonical_xy = (origin[0] + bbox[0], origin[1] + bbox[1])
        dx, dy = canonical_xy[0] - xy[0], canonical_xy[1] - xy[1]
        shape_equal = canonical_crop.size == binary(Image.open(ASSET_DIR[mode] / f'region-ua-{code}.png')).size
        rows.append((mode, region['iso'], xy[0], xy[1], canonical_xy[0], canonical_xy[1], dx, dy, xor, int(shape_equal), canonical_crop.size[0], canonical_crop.size[1]))
        total_xor += xor
        bad += int(xor != 0)
    print(f'{mode}: mismatched_regions={bad}/25 total_xor={total_xor}')

print('mode\tiso\tcur_x\tcur_y\tcanon_x\tcanon_y\tdx\tdy\txor\tsame_size\tcanon_w\tcanon_h')
for row in rows:
    print('\t'.join(map(str, row)))
