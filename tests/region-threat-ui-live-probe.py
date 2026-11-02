#!/usr/bin/env python3
import os
import asyncio
import re
from pathlib import Path
from aioesphomeapi import LogLevel
from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')
REGIONS = [3,4,5,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,31,9999]

def text_of(msg):
    value = getattr(msg, 'message', b'')
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else str(value)

async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []
    def on_log(msg):
        text = text_of(msg)
        if ('NEPTUN_THREATS_REGION ' in text or 'regionthreat112 ' in text or
                'NEPTUN valid=' in text or 'QA language=' in text):
            print(text)
            lines.append(text)
    stop = client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    original_lang = 1
    original_region = 31
    try:
        await verify_device(client, CONFIG)
        info = await client.device_info()
        print('VERSION=' + str(info.project_version))
        _, service_list = await client.list_entities_services()
        svc = {str(item.name): item for item in service_list}
        required = [
            'usp_neptun_refresh', 'usp_neptun_dump',
            'usp_neptun_region_threats_dump', 'usp_qa_select_region',
            'usp_qa_show_map', 'usp_qa_set_language',
            'usp_qa_dump_region_threat_ui_112'
        ]
        for name in required:
            if name not in svc:
                raise RuntimeError('missing service ' + name)

        await client.execute_service(svc['usp_qa_show_map'], {})
        await asyncio.sleep(0.6)
        await client.execute_service(svc['usp_qa_dump_region_threat_ui_112'], {})
        await asyncio.sleep(0.35)
        initial = next((x for x in reversed(lines) if 'regionthreat112 ' in x), '')
        m = re.search(r'lang=(\d+) region=(\d+)', initial)
        if m:
            original_lang = int(m.group(1)); original_region = int(m.group(2))
        print(f'ORIGINAL_LANGUAGE={original_lang}')
        print(f'ORIGINAL_REGION={original_region}')

        await client.execute_service(svc['usp_neptun_refresh'], {})
        await asyncio.sleep(6.0)
        await client.execute_service(svc['usp_neptun_dump'], {})
        await asyncio.sleep(0.4)

        region_totals = {}
        for region in REGIONS:
            await client.execute_service(
                svc['usp_neptun_region_threats_dump'], {'region_id': region})
            await asyncio.sleep(0.07)
        await asyncio.sleep(0.5)
        for line in lines:
            if 'NEPTUN_THREATS_REGION ' not in line:
                continue
            match = re.search(r'region=(\d+).*total=(\d+)', line)
            if match:
                region_totals[int(match.group(1))] = int(match.group(2))
        mapped = sum(region_totals.values())
        active = [r for r, total in region_totals.items() if total > 0]
        print('MAPPED_REGION_THREATS=' + str(mapped))
        print('ACTIVE_REGION_COUNT=' + str(len(active)))
        target = active[0] if active else original_region
        print('TARGET_REGION=' + str(target))

        await client.execute_service(svc['usp_qa_select_region'], {'region_id': target})
        await asyncio.sleep(0.7)
        ui_ok = True
        for lang in (0, 1, 2):
            await client.execute_service(svc['usp_qa_set_language'], {'language': lang})
            await asyncio.sleep(0.75)
            before = len(lines)
            await client.execute_service(svc['usp_qa_dump_region_threat_ui_112'], {})
            dump = ''
            for _ in range(12):
                fresh = [
                    x for x in lines[before:]
                    if 'regionthreat112 ' in x
                    and f'lang={lang} region={target}' in x
                ]
                if fresh:
                    dump = fresh[-1]
                    break
                await asyncio.sleep(0.25)
            match = re.search(r'total=(\d+)', dump)
            total = int(match.group(1)) if match else -1
            ok = f'lang={lang} region={target}' in dump and total == region_totals.get(target, 0)
            if total > 0:
                # The log message intentionally contains a real newline before
                # compact categories; inspect the whole message, not one regex line.
                ok = ok and any(tag in dump for tag in (
                    'БПЛА','UAV','FPV','РОЗВ','РАЗВ','REC','РКТ','MSL',
                    'BAL','KAB','MiG','?'))
            print(f'UI_LANG_{lang}=' + ('PASS' if ok else 'FAIL'))
            ui_ok &= ok

        await client.execute_service(svc['usp_qa_set_language'], {'language': original_lang})
        await asyncio.sleep(0.5)
        await client.execute_service(svc['usp_qa_select_region'], {'region_id': original_region})
        await asyncio.sleep(0.5)
        before = len(lines)
        await client.execute_service(svc['usp_qa_dump_region_threat_ui_112'], {})
        restored = ''
        for _ in range(12):
            fresh = [x for x in lines[before:] if 'regionthreat112 ' in x]
            if fresh:
                restored = fresh[-1]
                break
            await asyncio.sleep(0.25)
        restore_ok = f'lang={original_lang} region={original_region}' in restored
        print('STATE_RESTORE=' + ('PASS' if restore_ok else 'FAIL'))
        if not (ui_ok and restore_ok):
            raise RuntimeError('region threat UI probe failed')
        print('REGION_THREAT_UI_LIVE=PASS')
    finally:
        try: stop()
        except Exception: pass
        await client.disconnect()

asyncio.run(main())
