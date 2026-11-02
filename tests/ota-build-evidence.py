#!/usr/bin/env python3
"""Verify rollback flags in already-generated production/public ESP-IDF builds."""
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
BUILDS=(
    ROOT/'firmware/.esphome/build/ukraine-status-panel',
    ROOT/'firmware/.esphome/build/ukraine-status-panel-public-first-flash',
)

def need(ok,msg):
    if not ok: raise AssertionError(msg)

def check(build: Path):
    configs=[p for p in build.glob('sdkconfig.*') if not p.name.endswith('.esphomeinternal')]
    need(len(configs)==1,f'resolved sdkconfig missing/ambiguous: {build.name}')
    text=configs[0].read_text(errors='replace')
    need('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' in text,f'bootloader rollback disabled: {build.name}')
    need('CONFIG_APP_ROLLBACK_ENABLE=y' in text,f'app rollback disabled: {build.name}')
    generated=build/'src'
    sources='\n'.join(p.read_text(errors='replace') for p in generated.rglob('*') if p.is_file() and p.suffix in {'.h','.cpp'})
    need('USE_OTA_ROLLBACK' in sources,f'USE_OTA_ROLLBACK missing: {build.name}')

def main():
    for build in BUILDS: check(build)
    print('OTA_BUILD_EVIDENCE=PASS production+public rollback enabled')

if __name__=='__main__':
    try: main()
    except AssertionError as exc:
        print(f'OTA_BUILD_EVIDENCE=FAIL {exc}',file=sys.stderr); raise SystemExit(1)
