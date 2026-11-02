#!/usr/bin/env python3
import os
import asyncio
from pathlib import Path

from aioesphomeapi import LogLevel
from history_native_api_common import build_client

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')
SERVICES = (
    'usp_qa_show_power',
    'usp_qa_show_history',
    'usp_qa_show_weather',
    'usp_qa_show_weather_full',
    'usp_qa_show_map_full',
    'usp_qa_show_settings',
    'usp_qa_show_diagnostics',
    'usp_qa_show_home',
)


def text_of(msg):
    value = getattr(msg, 'message', b'')
    if isinstance(value, bytes):
        return value.decode('utf-8', errors='replace')
    return str(value)


async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []
    phase = 'startup'

    def on_log(msg):
        text = text_of(msg)
        if 'took a long time' in text or '[E]' in text or 'Guru' in text:
            print(f'{phase}: {text}')
            lines.append((phase, text))

    stop = client.subscribe_logs(
        on_log,
        log_level=LogLevel.LOG_LEVEL_DEBUG,
        dump_config=False,
    )
    try:
        info = await client.device_info()
        print('VERSION=' + str(info.project_version))
        _entities, service_list = await client.list_entities_services()
        services = {str(s.name): s for s in service_list}
        missing = [name for name in SERVICES if name not in services]
        if missing:
            raise RuntimeError(f'Missing QA services: {missing}')

        # API login/entity discovery can occasionally emit a one-off timing
        # warning while the debug log subscription is warming up. Report
        # startup warnings explicitly, then start the page-smoke measurement
        # from a settled session so they are not misattributed to a page.
        await asyncio.sleep(1.0)
        print(f'STARTUP_ISSUES={len(lines)}')
        lines.clear()
        phase = 'ready'

        for name in SERVICES:
            phase = name
            before = len(lines)
            await client.execute_service(services[name], {})
            await asyncio.sleep(1.3)
            count = len(lines) - before
            print(f'{name}_ISSUES={count}')

        phase = 'power-repeat'
        before = len(lines)
        for _ in range(3):
            await client.execute_service(services['usp_qa_show_power'], {})
            await asyncio.sleep(1.0)
        print(f'POWER_REPEAT_ISSUES={len(lines)-before}')
        print(f'TOTAL_ISSUES={len(lines)}')
        if lines:
            raise RuntimeError('UI page smoke observed runtime warnings/errors')
        print('R48_UI_PAGE_SMOKE=PASS')
    finally:
        try:
            stop()
        except Exception:
            pass
        await client.disconnect()


if __name__ == '__main__':
    asyncio.run(main())
