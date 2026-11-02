#!/usr/bin/env python3
"""Verify an absent optional SHT4x stays quiet across repeated probe cycles."""

from __future__ import annotations

import asyncio
import math
import os
from pathlib import Path

from aioesphomeapi import LogLevel
from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
SOAK_SECONDS = float(os.environ.get("USP_SHT4X_SOAK_SECONDS", "75"))
TARGET_NAMES = {"SHT4x Status", "Environment Temperature", "Environment Humidity"}


def message_text(message: object) -> str:
    """Return one Native API log message as text."""
    value = getattr(message, "message", b"")
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else str(value)


async def main() -> None:
    """Require NOT DETECTED plus no SHT/fatal warnings during the soak."""
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines: list[str] = []
    try:
        await verify_device(client, CONFIG)
        initial_info = await client.device_info()
        initial_version = str(getattr(initial_info, "project_version", ""))
        entities, _ = await client.list_entities_services()
        by_key = {getattr(item, "key", None): item for item in entities}
        names = {str(getattr(item, "name", "")) for item in entities}
        missing = sorted(TARGET_NAMES - names)
        if missing:
            raise RuntimeError(f"Missing SHT4x entities: {missing}")

        states: dict[str, object] = {}
        state_ready = asyncio.Event()

        def on_state(state: object) -> None:
            entity = by_key.get(getattr(state, "key", None))
            name = str(getattr(entity, "name", "")) if entity is not None else ""
            if name in TARGET_NAMES:
                states[name] = getattr(state, "state", None)
                if TARGET_NAMES.issubset(states):
                    state_ready.set()

        client.subscribe_states(on_state)
        try:
            await asyncio.wait_for(state_ready.wait(), timeout=12)
        except asyncio.TimeoutError:
            pass

        status = str(states.get("SHT4x Status", ""))
        if status != "NOT DETECTED":
            raise RuntimeError(f"Expected absent SHT4x, got status={status!r}")
        for name in ("Environment Temperature", "Environment Humidity"):
            value = states.get(name)
            if value is not None and not (isinstance(value, float) and math.isnan(value)):
                raise RuntimeError(f"Absent sensor published {name}={value!r}")
        print("SHT4X_ABSENCE_STATE=PASS status=NOT_DETECTED")

        def on_log(message: object) -> None:
            lines.append(message_text(message))

        stop_logs = client.subscribe_logs(
            on_log,
            log_level=LogLevel.LOG_LEVEL_WARN,
            dump_config=False,
        )
        try:
            await asyncio.sleep(SOAK_SECONDS)
        finally:
            try:
                stop_logs()
            except Exception:
                pass

        sht_faults = [line for line in lines if "sht4x" in line.lower() or "sensirion_i2c" in line.lower()]
        fatal_tokens = ("guru meditation", "task watchdog", "watchdog timeout", "assert failed", "panic", "abort()")
        fatals = [line for line in lines if any(token in line.lower() for token in fatal_tokens)]
        if sht_faults:
            raise RuntimeError("SHT4x warning/error during supported absence: " + " | ".join(sht_faults))
        if fatals:
            raise RuntimeError("Fatal runtime event during SHT4x soak: " + " | ".join(fatals))

        info = await client.device_info()
        final_version = str(getattr(info, "project_version", ""))
        if final_version != initial_version:
            raise RuntimeError(
                f"Firmware changed during soak: {initial_version!r} -> {final_version!r}"
            )
        print(
            "OPTIONAL_SHT4X_QUIET_LIVE=PASS "
            f"version={final_version} seconds={SOAK_SECONDS:.0f} warn_lines={len(lines)} "
            "bounded_reprobe_policy=1 sht_faults=0 fatals=0"
        )
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
