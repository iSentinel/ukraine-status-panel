#!/usr/bin/env python3
"""Validate OTA safety policy while browser OTA remains intentionally closed."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT=Path(__file__).resolve().parents[1]
PROD=(ROOT/'firmware/ukraine-status-panel.yaml').read_text()
PUBLIC_PATH=ROOT/'firmware/ukraine-status-panel-public-first-flash.yaml'

def need(ok,msg):
    if not ok: raise AssertionError(msg)

def block(text,start,end):
    return text.split(start,1)[1].split(end,1)[0]

def main():
    need(CURRENT_FIRMWARE_MARKER in PROD,'wrong production version')
    for token in (
        'name: "ukraine-status-panel.panel"',
        'board: esp32-s3-devkitc-1',
        'flash_size: 16MB',
        'type: esp-idf',
        'enable_ota_rollback: true',
        'safe_mode:',
        'boot_is_good_after: 1min',
        'num_attempts: 10',
        'reboot_timeout: 5min',
        'boot_is_good_on_shutdown: false',
        'platform: esphome',
        'encryption: {}',
    ):
        need(token in PROD,f'OTA readiness contract missing: {token}')
    web=block(PROD,'web_server:\n','\n# Shared bus:')
    need('  ota: false\n' in web,'browser OTA must remain disabled')
    for forbidden in ('disable_ota_downgrade: true','signed_ota_verification:'):
        need(forbidden not in PROD,f'premature signed/downgrade policy enabled: {forbidden}')
    need(PUBLIC_PATH.exists(),'public first-flash YAML missing')
    public=PUBLIC_PATH.read_text()
    for token in ('enable_ota_rollback: true','safe_mode:','boot_is_good_after: 1min','num_attempts: 10','reboot_timeout: 5min','boot_is_good_on_shutdown: false'):
        need(token in public,f'public rollback policy missing: {token}')
    need('web_server:\n' not in public,'public bootstrap must not expose production Web server')
    print('OTA_READINESS_STATIC=PASS')

if __name__=='__main__':
    try: main()
    except (AssertionError,ValueError) as exc:
        print(f'OTA_READINESS_STATIC=FAIL {exc}',file=sys.stderr); raise SystemExit(1)
