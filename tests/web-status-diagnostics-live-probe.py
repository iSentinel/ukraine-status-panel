#!/usr/bin/env python3
"""Validate the read-only Web diagnostics surface on a physical panel."""

from __future__ import annotations

import asyncio
import os
from pathlib import Path

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
EXPECTED = {
    "Firmware Version",
    "Panel IP",
    "Wi-Fi SSID",
    "Wi-Fi RSSI",
    "Uptime",
    "Free Heap",
    "Free PSRAM",
    "Panel Loop Stack Headroom",
    "Panel Battery Voltage",
    "Alert Data Age",
    "Alert Accepted Frames",
    "Alert Rejected Frames",
    "Alert Map Source",
    "Alert Live State",
    "Alert Live Revision",
    "Web Security Status",
}


async def main() -> None:
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    try:
        await verify_device(client, CONFIG)
        entities, _services = await client.list_entities_services()
        by_key = {getattr(entity, "key", None): entity for entity in entities}
        names = {str(getattr(entity, "name", "")) for entity in entities}
        missing = sorted(EXPECTED - names)
        if missing:
            raise RuntimeError(f"Missing diagnostics: {missing}")
        print(f"WEB_DIAG_ENTITY_SURFACE=PASS count={len(EXPECTED)}")

        values: dict[int, object] = {}
        done = asyncio.Event()

        def on_state(state) -> None:
            key = getattr(state, "key", None)
            if key is None:
                return
            values[key] = state
            present = {
                str(getattr(by_key.get(item_key), "name", ""))
                for item_key in values
                if item_key in by_key
            }
            if EXPECTED.issubset(present):
                done.set()

        client.subscribe_states(on_state)
        try:
            await asyncio.wait_for(done.wait(), timeout=10)
        except asyncio.TimeoutError:
            pass

        found = set()
        for key, state in values.items():
            entity = by_key.get(key)
            if entity is None:
                continue
            name = str(getattr(entity, "name", ""))
            if name not in EXPECTED:
                continue
            found.add(name)
            value = getattr(state, "state", None)
            if name in {"Wi-Fi SSID", "Panel IP"}:
                print(f"{name}=present")
            else:
                print(f"{name}={value}")

        missing_states = sorted(EXPECTED - found)
        if missing_states:
            raise RuntimeError(f"Missing diagnostic state values: {missing_states}")
        print(f"WEB_DIAG_STATE_SNAPSHOT=PASS count={len(found)}")
        print("WEB_DIAGNOSTICS_HARDWARE=PASS")
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
