#!/usr/bin/env python3
"""Opt-in Web entity round-trip and reboot-persistence probe."""
from __future__ import annotations

import asyncio
import os
from pathlib import Path
import re
import urllib.parse
import urllib.request

from aioesphomeapi import LogLevel
from history_native_api_common import build_client, secret_value, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware/ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')
BASE_URL = f'http://{ADDRESS}'
SNAPSHOT_RE = re.compile(
    r'SETTINGS134 language=(\d+) palette=(\d+) ui_region=(\d+) '
    r'stored_region=(\d+) outage=(\d+) backlight=([0-9.]+) '
    r'light=([0-9.]+) slider=(-?\d+)'
)
LANG = ('Українська', 'Русский', 'English')
PALETTE = ('Standard', 'CVD-friendly', 'Monochrome')


def enabled(name: str) -> bool:
    return os.environ.get(name, '0') == '1'


def require_opt_in() -> bool:
    if enabled('USP_ALLOW_PERSISTENCE_MUTATION') and enabled('USP_ALLOW_REBOOT'):
        return True
    print('WEB_CONFIG_PERSISTENCE=SKIP set USP_ALLOW_PERSISTENCE_MUTATION=1 and USP_ALLOW_REBOOT=1')
    return False


def opener() -> urllib.request.OpenerDirector:
    password = secret_value(CONFIG, 'web_server_password')
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, BASE_URL, 'admin', password)
    return urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))


def web_set(web, domain: str, name: str, params: dict[str, str]) -> None:
    entity = urllib.parse.quote(name, safe='')
    query = urllib.parse.urlencode(params)
    request = urllib.request.Request(
        f'{BASE_URL}/{domain}/{entity}/set?{query}', data=b'', method='POST')
    with web.open(request, timeout=10) as response:
        if response.status not in (200, 204):
            raise RuntimeError(f'Web update failed: {domain}/{name} -> {response.status}')


def log_text(message) -> str:
    value = getattr(message, 'message', b'')
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else str(value)


async def connect_session():
    last = None
    for _ in range(60):
        client = build_client(CONFIG, ADDRESS)
        try:
            await client.connect(login=True)
            await verify_device(client, CONFIG)
            lines: list[str] = []
            stop = client.subscribe_logs(
                lambda msg: lines.append(log_text(msg)),
                log_level=LogLevel.LOG_LEVEL_INFO,
                dump_config=False,
            )
            entities, services = await client.list_entities_services()
            service = {str(item.name): item for item in services}.get(
                'usp_qa_dump_shared_settings_134')
            if service is None:
                raise RuntimeError('Shared-settings snapshot service missing')
            return client, stop, lines, entities, service
        except Exception as exc:
            last = exc
            try:
                await client.disconnect()
            except Exception:
                pass
            await asyncio.sleep(1)
    raise RuntimeError(f'Device reconnect failed: {last}')


async def snapshot(client, service, lines: list[str]) -> tuple[int, ...]:
    start = len(lines)
    await client.execute_service(service, {})
    for _ in range(50):
        for line in lines[start:]:
            match = SNAPSHOT_RE.search(line)
            if match:
                values = match.groups()
                return tuple(int(value) for value in values[:5])
        await asyncio.sleep(0.1)
    raise RuntimeError('Settings snapshot timeout')


async def wait_snapshot(client, service, lines, expected, timeout=10.0):
    last = None
    for _ in range(max(1, int(timeout / 0.5))):
        last = await snapshot(client, service, lines)
        if tuple(last[:5]) == tuple(expected[:5]):
            return last
        await asyncio.sleep(0.5)
    raise RuntimeError(f'Settings did not converge: expected={expected[:5]} last={last}')


def by_name(entities) -> dict[str, object]:
    return {str(getattr(entity, 'name', '')): entity for entity in entities}


def region_option(entity, region_id: int) -> str:
    prefix = f'{region_id} ·'
    for option in getattr(entity, 'options', ()):
        if str(option).startswith(prefix):
            return str(option)
    raise RuntimeError(f'Region option not found: {region_id}')


def targets(original: tuple[int, ...], region_entity) -> tuple[int, ...]:
    language, palette, region, _stored, outage = original
    target_region = 14 if region == 31 else 31
    region_option(region_entity, target_region)
    target_outage = outage + 1 if outage < 60 else outage - 1
    return ((language + 1) % 3, (palette + 1) % 3,
            target_region, target_region, target_outage)


def apply_web(web, values, region_entity) -> None:
    language, palette, region, _stored, outage = values
    web_set(web, 'select', 'UI Language', {'option': LANG[language]})
    web_set(web, 'select', 'Alert Palette', {'option': PALETTE[palette]})
    web_set(web, 'select', 'Selected Region', {'option': region_option(region_entity, region)})
    web_set(web, 'number', 'Outage Group', {'value': str(outage)})


async def reboot(client, stop, entities) -> None:
    restart = by_name(entities).get('Restart')
    if restart is None:
        raise RuntimeError('Restart entity missing')
    key = int(getattr(restart, 'key'))
    client.button_command(key)
    await asyncio.sleep(0.5)
    stop()
    try:
        await client.disconnect()
    except Exception:
        pass


async def close_session(client, stop) -> None:
    stop()
    try:
        await client.disconnect()
    except Exception:
        pass


async def main() -> None:
    if not require_opt_in():
        return
    web = opener()
    client, stop, lines, entities, service = await connect_session()
    original = await snapshot(client, service, lines)
    entity_map = by_name(entities)
    region_entity = entity_map.get('Selected Region')
    if region_entity is None:
        raise RuntimeError('Selected Region entity missing')
    target = targets(original, region_entity)
    print(f'PERSISTENCE_ORIGINAL={original[:5]} target={target[:5]}')

    apply_web(web, target, region_entity)
    await wait_snapshot(client, service, lines, target)
    await asyncio.sleep(2)
    await reboot(client, stop, entities)

    client, stop, lines, entities, service = await connect_session()
    persisted = await wait_snapshot(client, service, lines, target)
    print(f'PERSISTENCE_AFTER_REBOOT=PASS values={persisted[:5]}')
    region_entity = by_name(entities).get('Selected Region')
    if region_entity is None:
        raise RuntimeError('Selected Region entity missing after reboot')

    apply_web(web, original, region_entity)
    await wait_snapshot(client, service, lines, original)
    await asyncio.sleep(2)
    await reboot(client, stop, entities)

    client, stop, lines, entities, service = await connect_session()
    restored = await wait_snapshot(client, service, lines, original)
    print(f'PERSISTENCE_RESTORE_AFTER_REBOOT=PASS values={restored[:5]}')
    await close_session(client, stop)
    print('WEB_CONFIG_PERSISTENCE=PASS')


if __name__ == '__main__':
    asyncio.run(main())
