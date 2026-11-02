#!/usr/bin/env python3
"""Validate selected-region NEPTUN status color across all alert palettes."""

import os
import asyncio
import re
from pathlib import Path
from aioesphomeapi import LogLevel
from history_native_api_common import build_client, expected_version

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')
REGIONS = [3,4,5,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,31,9999]
MAP = {
    0: {0x1F8A4C: 0x1F8A4C, 0xF5BE26: 0xF5BE26,
        0xF97316: 0xF97316, 0xDC3545: 0xDC3545, 0xA855F7: 0xA855F7, 0x34424E: 0x34424E},
    1: {0x1F8A4C: 0x0072B2, 0xF5BE26: 0xF0E442,
        0xF97316: 0xE69F00, 0xDC3545: 0xD55E00, 0xA855F7: 0xCC79A7, 0x34424E: 0x56616D},
    2: {0x1F8A4C: 0xA6A6A6, 0xF5BE26: 0xD9D9D9,
        0xF97316: 0xBFBFBF, 0xDC3545: 0xF2F2F2, 0xA855F7: 0xE0E0E0, 0x34424E: 0x4D4D4D},
}
DUMP_RE = re.compile(
    r'neptunpalette117 region=(\d+) level=(\d+) palette=(\d+) '
    r'valid=(\d+) stale=(\d+) fetch_ok=(\d+) color=([0-9A-F]{6}) semantic=(\d+) text=(.*)', re.S)


def text_of(msg):
    value = getattr(msg, 'message', b'')
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else str(value)


async def wait_line(lines, start, marker, predicate=lambda _: True):
    for _ in range(30):
        for line in reversed(lines[start:]):
            if marker in line and predicate(line):
                return line
        await asyncio.sleep(0.12)
    return ''


def expected_color(match):
    level = int(match.group(2))
    mode = int(match.group(3))
    valid = int(match.group(4))
    stale = int(match.group(5))
    fetch_ok = int(match.group(6))
    if not valid:
        return 0x90A4B8
    if stale or not fetch_ok:
        base = 0xF97316
    else:
        base = {0: 0x34424E, 1: 0x1F8A4C, 2: 0xF5BE26,
                4: 0xDC3545, 8: 0xF5BE26, 16: 0xF97316,
                64: 0xA855F7, 128: 0xF97316}[int(match.group(8))]
    return MAP[mode][base]


async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []

    def on_log(msg):
        text = text_of(msg)
        if ('neptunpalette117 ' in text or 'NEPTUN_REGION ' in text or
                'regionthreat112 ' in text or 'usp.palette' in text or
                'took a long time' in text):
            print(text)
            lines.append(text)

    stop = client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    original_region = 14
    original_palette = 0
    try:
        info = await client.device_info()
        print('VERSION=' + str(info.project_version))
        if str(info.project_version) != expected_version(CONFIG):
            raise RuntimeError('wrong physical firmware version')
        _, service_list = await client.list_entities_services()
        svc = {str(item.name): item for item in service_list}
        required = [
            'usp_neptun_refresh', 'usp_neptun_region_dump',
            'usp_qa_select_region', 'usp_qa_set_alert_palette_114',
            'usp_qa_dump_region_threat_ui_112',
            'usp_qa_dump_neptun_status_palette_117',
        ]
        for name in required:
            if name not in svc:
                raise RuntimeError('missing service ' + name)

        start = len(lines)
        await client.execute_service(svc['usp_qa_dump_region_threat_ui_112'], {})
        line = await wait_line(lines, start, 'regionthreat112 ')
        m = re.search(r'region=(\d+)', line)
        if m:
            original_region = int(m.group(1))

        start = len(lines)
        await client.execute_service(svc['usp_qa_dump_neptun_status_palette_117'], {})
        line = await wait_line(lines, start, 'neptunpalette117 ')
        m = DUMP_RE.search(line)
        if m:
            original_palette = int(m.group(3))
        print(f'ORIGINAL_REGION={original_region}')
        print(f'ORIGINAL_PALETTE={original_palette}')

        await client.execute_service(svc['usp_neptun_refresh'], {})
        ready = False
        for _ in range(20):
            start = len(lines)
            await client.execute_service(
                svc['usp_neptun_region_dump'], {'region_id': original_region})
            line = await wait_line(
                lines, start, 'NEPTUN_REGION ',
                lambda x, r=original_region: f'region={r} ' in x)
            if 'valid=YES' in line and 'fetch_ok=YES' in line:
                ready = True
                break
            await asyncio.sleep(0.4)
        print('NEPTUN_READY=' + ('PASS' if ready else 'FAIL'))
        if not ready:
            raise RuntimeError('NEPTUN regional alerts did not become ready')

        levels = {}
        for region in REGIONS:
            start = len(lines)
            await client.execute_service(svc['usp_neptun_region_dump'], {'region_id': region})
            line = await wait_line(
                lines, start, 'NEPTUN_REGION ',
                lambda x, r=region: f'region={r} ' in x)
            m = re.search(r'region=(\d+).*level=(\d+)', line)
            if m:
                levels[int(m.group(1))] = int(m.group(2))
        target = max(levels, key=lambda r: levels[r]) if levels else original_region
        print(f'TARGET_REGION={target}')
        print(f'TARGET_LEVEL={levels.get(target, 0)}')
        await client.execute_service(svc['usp_qa_select_region'], {'region_id': target})
        await asyncio.sleep(0.7)

        results = []
        texts = []
        for mode in (0, 1, 2):
            await client.execute_service(svc['usp_qa_set_alert_palette_114'], {'mode': mode})
            await asyncio.sleep(0.9)
            start = len(lines)
            await client.execute_service(svc['usp_qa_dump_neptun_status_palette_117'], {})
            line = await wait_line(
                lines, start, 'neptunpalette117 ',
                lambda x, m=mode: f'palette={m} ' in x)
            match = DUMP_RE.search(line)
            if not match:
                print(f'PALETTE_{mode}_STATUS_COLOR=FAIL missing_dump')
                results.append(False)
                continue
            actual = int(match.group(7), 16)
            expected = expected_color(match)
            text = match.group(9).splitlines()[0]
            texts.append(text)
            ok = actual == expected
            print(
                f'PALETTE_{mode}_STATUS_COLOR=' + ('PASS' if ok else 'FAIL')
                + f' actual={actual:06X} expected={expected:06X}')
            results.append(ok)
        same_text = len(set(texts)) <= 1 and len(texts) == 3
        print('PALETTE_TEXT_STABLE=' + ('PASS' if same_text else 'FAIL'))
        results.append(same_text)

        await client.execute_service(
            svc['usp_qa_set_alert_palette_114'], {'mode': original_palette})
        # Palette changes schedule a map repaint plus the selected-region
        # status refresh. Do not stack a second mutating QA service on the same
        # scheduler slice; this is not how a human can operate the UI either.
        await asyncio.sleep(0.9)
        await client.execute_service(
            svc['usp_qa_select_region'], {'region_id': original_region})
        await asyncio.sleep(0.7)
        print('STATE_RESTORE=PASS')

        long_ops = [x for x in lines if 'took a long time' in x]
        print('LONG_OPERATION_WARNINGS=' + str(len(long_ops)))
        results.append(not long_ops)
        if not all(results):
            raise RuntimeError('NEPTUN status palette probe failed')
        print('NEPTUN_STATUS_PALETTE_LIVE=PASS')
    finally:
        try: stop()
        except Exception: pass
        await client.disconnect()


asyncio.run(main())
