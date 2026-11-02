#!/usr/bin/env python3
"""Validate 0.115.116 semantic NEPTUN region status in UA/RU/EN."""

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
EXPECTED = {
    0: {0: 'Немає активної', 1: 'Нет активной', 2: 'No active'},
    1: {0: 'Повітряна загроза', 1: 'Воздушная угроза', 2: 'Air threat'},
    2: {0: 'Повітряна тривога', 1: 'Воздушная тревога', 2: 'Air alert'},
}
FORBIDDEN = ('Yellow', 'Red', 'Жёлт', 'Красн', 'Жовт', 'Червон')


def text_of(msg):
    value = getattr(msg, 'message', b'')
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else str(value)


async def wait_line(lines, start, marker, predicate=lambda _: True):
    for _ in range(25):
        for line in reversed(lines[start:]):
            if marker in line and predicate(line):
                return line
        await asyncio.sleep(0.15)
    return ''


async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []

    def on_log(msg):
        text = text_of(msg)
        if ('NEPTUN_REGION ' in text or 'regionthreat112 ' in text or
                'QA language=' in text or 'took a long time' in text):
            print(text)
            lines.append(text)

    stop = client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    original_lang = 1
    original_region = 31
    try:
        info = await client.device_info()
        print('VERSION=' + str(info.project_version))
        if str(info.project_version) != expected_version(CONFIG):
            raise RuntimeError('wrong physical firmware version')

        _, service_list = await client.list_entities_services()
        svc = {str(item.name): item for item in service_list}
        required = [
            'usp_neptun_refresh', 'usp_neptun_region_dump',
            'usp_qa_select_region', 'usp_qa_show_map',
            'usp_qa_set_language', 'usp_qa_dump_region_threat_ui_112',
        ]
        for name in required:
            if name not in svc:
                raise RuntimeError('missing service ' + name)

        await client.execute_service(svc['usp_qa_show_map'], {})
        await asyncio.sleep(0.6)
        start = len(lines)
        await client.execute_service(svc['usp_qa_dump_region_threat_ui_112'], {})
        initial = await wait_line(lines, start, 'regionthreat112 ')
        match = re.search(r'lang=(\d+) region=(\d+)', initial)
        if match:
            original_lang = int(match.group(1))
            original_region = int(match.group(2))
        print(f'ORIGINAL_LANGUAGE={original_lang}')
        print(f'ORIGINAL_REGION={original_region}')

        # A reboot may occur just before the scheduled NEPTUN fetch. Force one
        # refresh and wait until the regional-alert snapshot becomes valid so
        # target selection and the UI comparison use the same generation.
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
            raise RuntimeError('NEPTUN regional alert snapshot did not become ready')

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
        level = levels.get(target, 0)
        print(f'TARGET_REGION={target}')
        print(f'TARGET_LEVEL={level}')

        await client.execute_service(svc['usp_qa_select_region'], {'region_id': target})
        await asyncio.sleep(0.6)
        results = []
        for language in (0, 1, 2):
            await client.execute_service(svc['usp_qa_set_language'], {'language': language})
            await asyncio.sleep(0.7)
            start = len(lines)
            await client.execute_service(svc['usp_qa_dump_region_threat_ui_112'], {})
            dump = await wait_line(
                lines, start, 'regionthreat112 ',
                lambda x, l=language, r=target:
                    f'lang={l} region={r}' in x)
            expected = EXPECTED[min(level, 2)][language]
            text = dump.split('text=', 1)[1] if 'text=' in dump else ''
            ok = text.startswith(expected) and not any(word in text for word in FORBIDDEN)
            print(f'LANG_{language}_SEMANTIC=' + ('PASS' if ok else 'FAIL'))
            print(f'LANG_{language}_EXPECTED={expected}')
            results.append(ok)

        await client.execute_service(svc['usp_qa_set_language'], {'language': original_lang})
        await client.execute_service(svc['usp_qa_select_region'], {'region_id': original_region})
        await asyncio.sleep(0.6)
        print('STATE_RESTORE=PASS')

        long_ops = [x for x in lines if 'took a long time' in x]
        print('LONG_OPERATION_WARNINGS=' + str(len(long_ops)))
        if not all(results) or long_ops:
            raise RuntimeError('semantic region status probe failed')
        print('SEMANTIC_REGION_STATUS_LIVE=PASS')
    finally:
        try: stop()
        except Exception: pass
        await client.disconnect()


asyncio.run(main())
