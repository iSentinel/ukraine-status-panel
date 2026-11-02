#!/usr/bin/env python3
"""Ensure normal interactive settings never force a global NVS flush."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    'outage': (ROOT / 'components/usp_outage/outage_client.cpp', 'save_group_preference_'),
    'weather': (ROOT / 'components/usp_weather/weather_client.cpp', 'save_city_preference_'),
    'region': (ROOT / 'components/usp_region/region_settings.cpp', 'save_selected_region_preference_'),
}


def body(path: Path, name: str) -> str:
    text = path.read_text()
    match = re.search(rf'bool\s+[^\n]*{re.escape(name)}\([^)]*\)\s*\{{(?P<body>.*?)\n\}}', text, re.S)
    if match is None:
        raise AssertionError(f'missing function {name} in {path.name}')
    return match.group('body')


def main() -> None:
    for label, (path, name) in CASES.items():
        source = body(path, name)
        if 'global_preferences->sync()' in source:
            raise AssertionError(f'{label} interactive save forces global sync')
        if '.save(' not in source:
            raise AssertionError(f'{label} no longer queues a preference')
    yaml = (ROOT / 'firmware/ukraine-status-panel.yaml').read_text()
    if 'flash_write_interval:' in yaml and 'flash_write_interval: 60s' not in yaml:
        raise AssertionError('interactive-save contract requires default/60s IntervalSyncer')
    print('INTERACTIVE_PREFERENCE_SYNC_STATIC=PASS paths=3 forced_global_sync=0 interval=default-60s')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'INTERACTIVE_PREFERENCE_SYNC_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
