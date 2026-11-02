#!/usr/bin/env python3
"""Exercise all 0.115.114 alert palette modes over the encrypted Native API."""

import os
import asyncio
import re
from pathlib import Path

from aioesphomeapi import LogLevel

from history_native_api_common import build_client, expected_version

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')
EXPECTED = {
    0: ('34424E', '1F8A4C', 'F5BE26', 'F97316', 'DC3545', '9C27B0'),
    1: ('56616D', '0072B2', 'F0E442', 'E69F00', 'D55E00', 'CC79A7'),
    2: ('4D4D4D', 'A6A6A6', 'D9D9D9', 'BFBFBF', 'F2F2F2', '737373'),
}


def text_of(msg):
    value = getattr(msg, 'message', b'')
    if isinstance(value, bytes):
        return value.decode('utf-8', errors='replace')
    return str(value)


async def wait_dump(lines, start, mode):
    for _ in range(20):
        fresh = [line for line in lines[start:] if 'palette114 mode=' in line]
        if fresh:
            line = fresh[-1]
            match = re.search(
                r'mode=(\d+) nodata=([0-9A-F]{6}) clear=([0-9A-F]{6}) '
                r'uav=([0-9A-F]{6}) partial=([0-9A-F]{6}) '
                r'alert=([0-9A-F]{6}) event=([0-9A-F]{6})',
                line,
            )
            if match and int(match.group(1)) == mode:
                return line, tuple(match.groups()[1:])
        await asyncio.sleep(0.15)
    return '', ()


async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []

    def on_log(msg):
        text = text_of(msg)
        lines.append(text)
        if 'usp.palette' in text or 'took a long time' in text:
            print(text)

    stop = client.subscribe_logs(
        on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    try:
        info = await client.device_info()
        print('VERSION=' + str(info.project_version))
        if str(info.project_version) != expected_version(CONFIG):
            raise RuntimeError('wrong physical firmware version')

        _, service_list = await client.list_entities_services()
        services = {str(item.name): item for item in service_list}
        set_name = 'usp_qa_set_alert_palette_114'
        dump_name = 'usp_qa_dump_alert_palette_114'
        for name in (set_name, dump_name):
            if name not in services:
                raise RuntimeError('missing service ' + name)

        results = []
        for mode in (0, 1, 2, 0):
            start = len(lines)
            await client.execute_service(services[set_name], {'mode': mode})
            await asyncio.sleep(1.4)
            await client.execute_service(services[dump_name], {})
            line, colors = await wait_dump(lines, start, mode)
            ok = colors == EXPECTED[mode]
            print(f'PALETTE_MODE_{mode}=' + ('PASS' if ok else 'FAIL'))
            print('PALETTE_COLORS_' + str(mode) + '=' + ','.join(colors))
            results.append(ok)
            await asyncio.sleep(0.35)

        long_ops = [line for line in lines if 'took a long time' in line]
        print('LONG_OPERATION_WARNINGS=' + str(len(long_ops)))
        print('FINAL_MODE=0')
        if not all(results) or long_ops:
            raise RuntimeError('palette live probe failed')
        print('PALETTE114_LIVE=PASS')
    finally:
        try:
            stop()
        except Exception:
            pass
        await client.disconnect()


asyncio.run(main())
