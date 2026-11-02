#!/usr/bin/env python3
import os
import asyncio
import re
from pathlib import Path
from aioesphomeapi import LogLevel
from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
OUTAGE_CPP = ROOT / 'components' / 'usp_outage' / 'outage_client.cpp'
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')


def text_of(msg):
    value = getattr(msg, 'message', b'')
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else str(value)


def static_power_contract():
    yaml = CONFIG.read_text(encoding='utf-8')
    cpp = OUTAGE_CPP.read_text(encoding='utf-8')
    block = re.search(r'id: i18n_043\n(?P<body>(?:\s+[^\n]*\n){1,12})', yaml)
    body = block.group('body') if block else ''
    return {
        'width340': 'width: 340' in body,
        'font26': 'text_font: usp_i18n_26' in body,
        'ru_emergency': 'Экстренные отключения' in cpp,
    }


async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []
    def on_log(msg):
        text = text_of(msg)
        if 'Map selection' in text or 'power060' in text:
            print(text)
            lines.append(text)
    stop = client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    try:
        await verify_device(client, CONFIG)
        info = await client.device_info()
        print(f'VERSION={info.project_version}')
        _entities, service_list = await client.list_entities_services()
        services = {str(s.name): s for s in service_list}
        for name in ('usp_qa_set_language','usp_qa_map_touch','usp_qa_show_power','usp_qa_dump_power_status_060'):
            if name not in services:
                raise RuntimeError(f'missing service: {name}')
        await client.execute_service(services['usp_qa_set_language'], {'language': 1})
        await asyncio.sleep(0.7)
        await client.execute_service(services['usp_qa_map_touch'], {'map_x': 292, 'map_y': 136})
        await asyncio.sleep(0.9)
        await client.execute_service(services['usp_qa_map_touch'], {'map_x': 270, 'map_y': 120})
        await asyncio.sleep(0.9)
        await client.execute_service(services['usp_qa_show_power'], {})
        await asyncio.sleep(0.7)
        await client.execute_service(services['usp_qa_dump_power_status_060'], {})
        await asyncio.sleep(0.8)

        kyiv = any('x=292 y=136 region=31' in x and 'Киев' in x for x in lines)
        oblast = any('x=270 y=120 region=14' in x and 'Киевская' in x for x in lines)
        power_line = next((x for x in reversed(lines) if 'power060 lang=1' in x), '')
        dims = re.search(r'width=(\d+) height=(\d+) text=(.*?)(?:\x1b|$)', power_line)
        live_layout = bool(dims and int(dims.group(1)) >= 340 and int(dims.group(2)) >= 35)
        contract = static_power_contract()
        static_ok = all(contract.values())
        print('KYIV_CITY=' + ('PASS' if kyiv else 'FAIL'))
        print('KYIV_OBLAST=' + ('PASS' if oblast else 'FAIL'))
        print('RU_POWER_LIVE_LAYOUT=' + ('PASS' if live_layout else 'FAIL'))
        print('RU_POWER_STATIC_WIDTH340=' + ('PASS' if contract['width340'] else 'FAIL'))
        print('RU_POWER_STATIC_FONT26=' + ('PASS' if contract['font26'] else 'FAIL'))
        print('RU_EMERGENCY_TRANSLATION=' + ('PASS' if contract['ru_emergency'] else 'FAIL'))
        if dims:
            print('RU_POWER_LIVE_TEXT=' + dims.group(3))
        if not (kyiv and oblast and live_layout and static_ok):
            raise RuntimeError('UI regression v2 failed')
        print('UI_REGRESSION_LIVE_V2=PASS')
    finally:
        try: stop()
        except Exception: pass
        await client.disconnect()

asyncio.run(main())
