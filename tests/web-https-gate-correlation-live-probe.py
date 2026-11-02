#!/usr/bin/env python3
"""Correlate local Web latency with serialized background HTTPS activity."""
from __future__ import annotations

import asyncio
import os
from pathlib import Path
import re
import statistics
import time
import urllib.request

from aioesphomeapi import LogLevel

from history_native_api_common import (
    build_client, expected_version, secret_value, verify_device,
)

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
BASE = f"http://{ADDRESS}"
DURATION = float(os.environ.get("USP_GATE_CORRELATION_SECONDS", "75"))
INTERVAL = float(os.environ.get("USP_GATE_WEB_INTERVAL", "0.5"))
TIMEOUT = float(os.environ.get("USP_GATE_WEB_TIMEOUT", "3.0"))
SLOW = float(os.environ.get("USP_GATE_WEB_SLOW", "0.5"))
TRIGGER_WEATHER = os.environ.get("USP_GATE_TRIGGER_WEATHER", "1") == "1"
GATE_RE = re.compile(r"HTTPS_GATE_(WAIT|ACQUIRE|RELEASE) owner=([^ ]+)")


def web_opener() -> urllib.request.OpenerDirector:
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, BASE, "admin", secret_value(CONFIG, "web_server_password"))
    return urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))


def fetch(opener: urllib.request.OpenerDirector, path: str) -> tuple[bool, float, str]:
    started = time.monotonic()
    try:
        request = urllib.request.Request(BASE + path, headers={"Connection": "close"})
        with opener.open(request, timeout=TIMEOUT) as response:
            body = response.read()
            if response.status != 200:
                return False, time.monotonic() - started, f"http_{response.status}"
            return True, time.monotonic() - started, f"bytes={len(body)}"
    except Exception as exc:  # diagnostic probe records the exact transport failure
        return False, time.monotonic() - started, type(exc).__name__


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * fraction))
    return ordered[index]


async def main() -> None:
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    await verify_device(client, CONFIG)
    wanted_version = expected_version(CONFIG)
    entities, _ = await client.list_entities_services()
    weather = next((e for e in entities if str(getattr(e, "name", "")) == "Refresh Weather"), None)
    gate_events: list[tuple[float, str, str, str]] = []
    samples: list[tuple[float, float, str, bool, str]] = []
    api_samples: list[tuple[float, bool, float]] = []
    started = time.monotonic()

    def on_log(message) -> None:
        raw = getattr(message, "message", b"")
        text = raw.decode("utf-8", errors="replace") if isinstance(raw, bytes) else str(raw)
        match = GATE_RE.search(text)
        if match:
            stamp = time.monotonic()
            gate_events.append((stamp, match.group(1), match.group(2), text.strip()))
            print(f"GATE_EVENT t={stamp-started:.3f} {text.strip()}")

    stop_logs = client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False)
    opener = web_opener()
    try:
        if TRIGGER_WEATHER and weather is not None:
            await asyncio.sleep(2.0)
            client.button_command(int(weather.key))
            print("GATE_TRIGGER_WEATHER=1")
        deadline = started + DURATION
        index = 0
        next_api = time.monotonic()
        while time.monotonic() < deadline:
            path = "/" if index % 2 == 0 else "/0.js"
            req_start = time.monotonic()
            ok, elapsed, detail = await asyncio.to_thread(fetch, opener, path)
            req_end = time.monotonic()
            samples.append((req_start, req_end, path, ok, detail))
            if not ok or elapsed >= SLOW:
                print(f"WEB_SAMPLE t={req_start-started:.3f} path={path} ok={int(ok)} "
                      f"seconds={elapsed:.3f} detail={detail}")
            index += 1
            now = time.monotonic()
            if now >= next_api:
                api_start = now
                try:
                    info = await asyncio.wait_for(client.device_info(), timeout=2.0)
                    api_ok = str(info.project_version) == wanted_version
                except Exception:
                    api_ok = False
                api_elapsed = time.monotonic() - api_start
                api_samples.append((api_start, api_ok, api_elapsed))
                next_api = now + 5.0
            await asyncio.sleep(max(0.0, INTERVAL - (time.monotonic() - req_end)))
    finally:
        stop_logs()
        await client.disconnect()

    intervals: list[tuple[float, float, str]] = []
    active: tuple[float, str] | None = None
    for stamp, kind, owner, _text in gate_events:
        if kind == "ACQUIRE":
            active = (stamp, owner)
        elif kind == "RELEASE" and active is not None:
            intervals.append((active[0], stamp, active[1]))
            active = None
    if active is not None:
        intervals.append((active[0], time.monotonic(), active[1]))

    durations = [end - begin for begin, end, _path, ok, _detail in samples if ok]
    failures = [item for item in samples if not item[3]]
    slow = [item for item in samples if item[3] and item[1] - item[0] >= SLOW]
    impacted = failures + slow
    overlap = 0
    for begin, end, path, ok, detail in impacted:
        owners = sorted({owner for left, right, owner in intervals if left <= end and right >= begin})
        if owners:
            overlap += 1
        print(f"WEB_CORRELATION path={path} ok={int(ok)} seconds={end-begin:.3f} "
              f"gate={','.join(owners) if owners else 'none'} detail={detail}")

    api_failures = sum(1 for _t, ok, _elapsed in api_samples if not ok)
    print(f"WEB_GATE_SUMMARY samples={len(samples)} failures={len(failures)} slow={len(slow)} "
          f"p50={statistics.median(durations) if durations else 0.0:.3f} "
          f"p95={percentile(durations, 0.95):.3f} max={max(durations) if durations else 0.0:.3f} "
          f"gate_events={len(gate_events)} impacted_with_gate={overlap} api_failures={api_failures}")
    if api_failures:
        raise RuntimeError(f"Native API heartbeat failed {api_failures} times")
    if not gate_events:
        raise RuntimeError("No HTTPS gate events observed; correlation window was inconclusive")
    print("WEB_HTTPS_GATE_CORRELATION_LIVE=PASS")


if __name__ == "__main__":
    asyncio.run(main())
