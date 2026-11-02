#!/usr/bin/env python3
"""Verify that identical Home alert frames skip redundant LVGL refreshes."""

import asyncio
import os
import re
from pathlib import Path

from aioesphomeapi import LogLevel

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get(
    'USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')
CACHE_RE = re.compile(
    r'DASH_ALERT_CACHE valid=(\d+) signature=([0-9A-F]{16}) '
    r'hits=(\d+) skips=(\d+)')


def text_of(message):
    """Return one Native API log message as text."""
    value = getattr(message, 'message', b'')
    if isinstance(value, bytes):
        return value.decode('utf-8', errors='replace')
    return str(value)


async def cache_state(client, service, lines):
    """Request and parse the latest dashboard alert cache counters."""
    marker = len(lines)
    await client.execute_service(service, {})
    for _ in range(30):
        for line in reversed(lines[marker:]):
            match = CACHE_RE.search(line)
            if match:
                valid, signature, hits, skips = match.groups()
                return {
                    'valid': int(valid),
                    'signature': signature,
                    'hits': int(hits),
                    'skips': int(skips),
                }
        await asyncio.sleep(0.1)
    raise RuntimeError('dashboard alert cache dump timeout')


async def apply_frame(client, service, revision, states):
    """Apply one deterministic 25-region alert frame and let LVGL settle."""
    await client.execute_service(
        service, {'revision': revision, 'states': states})
    await asyncio.sleep(0.8)


async def main():
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines = []

    def on_log(message):
        text = text_of(message)
        lines.append(text)
        if 'DASH_ALERT_CACHE' in text or 'took a long time' in text:
            print(text)

    stop = client.subscribe_logs(
        on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    feed_disabled = False
    services = {}
    try:
        await verify_device(client, CONFIG)
        _entities, service_list = await client.list_entities_services()
        services = {str(item.name): item for item in service_list}
        required = {
            'usp_qa_show_home',
            'usp_apply_alert_frame',
            'usp_qa_dashboard_alert_cache_dump',
            'usp_qa_clear_alert_override',
        }
        missing = required - services.keys()
        if missing:
            raise RuntimeError('missing services: ' + ', '.join(sorted(missing)))

        await client.execute_service(services['usp_qa_show_home'], {})
        await asyncio.sleep(0.4)
        feed_disabled = True
        await asyncio.sleep(0.25)

        frame_a = [1] * 25
        frame_b = frame_a.copy()
        frame_b[0] = 2

        await apply_frame(
            client, services['usp_apply_alert_frame'], 'cache-a-1', frame_a)
        state_a = await cache_state(
            client, services['usp_qa_dashboard_alert_cache_dump'], lines)

        await apply_frame(
            client, services['usp_apply_alert_frame'], 'cache-a-2', frame_a)
        state_a_repeat = await cache_state(
            client, services['usp_qa_dashboard_alert_cache_dump'], lines)

        await apply_frame(
            client, services['usp_apply_alert_frame'], 'cache-b-1', frame_b)
        state_b = await cache_state(
            client, services['usp_qa_dashboard_alert_cache_dump'], lines)

        await apply_frame(
            client, services['usp_apply_alert_frame'], 'cache-b-2', frame_b)
        state_b_repeat = await cache_state(
            client, services['usp_qa_dashboard_alert_cache_dump'], lines)

        same_a_skipped = (
            state_a_repeat['hits'] == state_a['hits']
            and state_a_repeat['skips'] >= state_a['skips'] + 1
        )
        changed_b_hit = (
            state_b['hits'] >= state_a_repeat['hits'] + 1
            and state_b['signature'] != state_a_repeat['signature']
        )
        same_b_skipped = (
            state_b_repeat['hits'] == state_b['hits']
            and state_b_repeat['skips'] >= state_b['skips'] + 1
        )
        long_ops = [line for line in lines if 'took a long time' in line]

        print('A_REPEAT_SKIP=' + ('PASS' if same_a_skipped else 'FAIL'))
        print('B_CHANGE_HIT=' + ('PASS' if changed_b_hit else 'FAIL'))
        print('B_REPEAT_SKIP=' + ('PASS' if same_b_skipped else 'FAIL'))
        print('CACHE_HITS_DELTA=' + str(
            state_b_repeat['hits'] - state_a['hits']))
        print('CACHE_SKIPS_DELTA=' + str(
            state_b_repeat['skips'] - state_a['skips']))
        print('LONG_OPERATION_WARNINGS=' + str(len(long_ops)))
        if not all((same_a_skipped, changed_b_hit, same_b_skipped)):
            raise RuntimeError('dashboard alert signature gate failed')
        if long_ops:
            raise RuntimeError('dashboard alert cache probe saw LVGL long-op')
        print('DASHBOARD_ALERT_CACHE_LIVE=PASS')
    finally:
        if feed_disabled and services:
            try:
                await client.execute_service(
                    services['usp_qa_clear_alert_override'], {})
                await asyncio.sleep(4.0)
            except Exception:
                pass
        try:
            stop()
        except Exception:
            pass
        await client.disconnect()


asyncio.run(main())
