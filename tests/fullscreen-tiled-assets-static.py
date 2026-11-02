#!/usr/bin/env python3
"""Guard the fullscreen tiled-outline rendering contract."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / 'firmware' / 'ukraine-status-panel.yaml'
META = ROOT / 'assets/dashboard-map-0.115.44/fullscreen-tiled/metadata.json'

text = YAML.read_text(encoding='utf-8')
meta = json.loads(META.read_text(encoding='utf-8'))
if meta['tile_count'] != 42 or meta['grid_cols'] != 10 or meta['grid_rows'] != 6:
    raise RuntimeError('unexpected fullscreen tile grid metadata')
if meta['reconstruction_diff_pixels'] != 0:
    raise RuntimeError('outline tile reconstruction is not exact')
if 'fullscreen_map_base_fast' in text or 'base-rgb565.png' in text:
    raise RuntimeError('RGB565 fullscreen base is still compiled')
if 'id: fullscreen_map_origin' not in text:
    raise RuntimeError('fullscreen map origin anchor is missing')
if 'fullscreen-fast/masks-binary/region-ua-05.png' not in text:
    raise RuntimeError('outline-subtracted fullscreen region masks are not active')
for rec in meta['tiles']:
    if f'id: {rec["id"]}_mask' not in text:
        raise RuntimeError(f'missing image definition {rec["id"]}_mask')
    if f'id: {rec["id"]}\n' not in text:
        raise RuntimeError(f'missing LVGL tile widget {rec["id"]}')
print(
    'FULLSCREEN_TILED_ASSETS_STATIC=PASS '
    f'grid={meta["grid_cols"]}x{meta["grid_rows"]} tiles={meta["tile_count"]} '
    f'raw_binary_estimate={meta["raw_binary_bytes_estimate"]}'
)
