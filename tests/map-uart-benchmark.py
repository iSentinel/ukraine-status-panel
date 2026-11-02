#!/usr/bin/env python3
"""Measure map refreshes through UART and journal each device command once."""

import argparse
import asyncio
from contextlib import suppress
import json
import math
import os
from pathlib import Path
import re
import statistics
import time

from loguru import logger

from history_native_api_common import build_client

ROOT = Path(__file__).resolve().parents[1]


def arguments():
    """Read the explicit hardware target, output and existing UART capture."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--uart-log", required=True, type=Path)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--address", default="192.0.2.208")
    parser.add_argument("--cold-reboots", type=int, default=10)
    parser.add_argument("--boot-settle", type=float, default=75)
    parser.add_argument("--start-index", type=int, default=0)
    args = parser.parse_args()
    if not 1 <= args.cold_reboots <= 10 or args.boot_settle < 65:
        parser.error(
            "Use 1..10 reboots and at least 65 seconds between confirmed boots"
        )
    if not args.uart_log.is_file():
        parser.error("Start the independent UART recorder first")
    return args


def save(path, value):
    """Persist before sending a command and replace the journal atomically."""
    temporary = path.with_suffix(".tmp")
    with temporary.open("w") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def uart_since(path, offset):
    """Read only bytes appended after a recorded command intention."""
    with path.open("rb") as stream:
        stream.seek(offset)
        return stream.read().decode("utf-8", "replace")


async def connect(args):
    """Retry read-only connection setup; never resend device commands."""
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        client = build_client(
            ROOT / "firmware/ukraine-status-panel.yaml", args.address
        )
        try:
            await asyncio.wait_for(client.connect(login=True), 12)
            info = await client.device_info()
            if (
                info.name != "ukraine-status-panel"
                or info.project_version != args.expected_version
            ):
                raise RuntimeError("Unexpected panel identity or version")
            entities, services = await client.list_entities_services()
            restart = next(
                item.key for item in entities if item.name == "Restart"
            )
            uptime_key = next(
                item.key for item in entities if item.name == "Uptime"
            )
            return (
                client,
                {item.name: item for item in services},
                restart,
                uptime_key,
            )
        except Exception as error:
            await client.disconnect(force=True)
            if isinstance(error, RuntimeError):
                raise
            logger.debug(
                "Read-only connection not ready: {}", type(error).__name__
            )
            await asyncio.sleep(1)
    raise RuntimeError("Read-only connection deadline expired")


async def restart_once(session, args, state, progress):
    """Confirm a new boot in independent UART bytes, without uptime guesses."""
    client, _services, key, _uptime_key = session
    offset = args.uart_log.stat().st_size
    state["operation"] = {
        "kind": "restart",
        "status": "prepared",
        "sample": state["next_sample"],
        "uart_offset": offset,
        "started_at": time.time(),
    }
    save(progress, state)
    client.button_command(key)
    state["operation"]["status"] = "in_doubt"
    save(progress, state)
    deadline = time.monotonic() + 90
    version = (
        f"Project ukraine-status-panel.panel version {args.expected_version}"
    )
    while time.monotonic() < deadline:
        text = uart_since(args.uart_log, offset)
        if "LVGL_REFRESH_PROFILE seq=0 " in text and version in text:
            state["operation"]["status"] = "confirmed_by_uart_boot"
            save(progress, state)
            await client.disconnect(force=True)
            return await connect(args), time.monotonic()
        await asyncio.sleep(0.5)
    raise RuntimeError(
        "Restart not confirmed; journal retained; do not replay"
    )


def sample_result(text, mode, index):
    """Preserve missing data and distinguish handler time from map presentation."""
    rows = []
    for line in text.splitlines():
        if "LVGL_REFRESH_PROFILE" in line:
            rows.append(
                {
                    key: int(value)
                    for key, value in re.findall(r"(\w+_ms)=(\d+)", line)
                }
            )
    present = [
        int(value)
        for value in re.findall(
            r"FULLSCREEN_CACHE_PRESENT seq=\d+ map_refresh_ms=(\d+)", text
        )
    ]
    fatal = re.search(
        r"Guru Meditation|watchdog got triggered|assert failed|panic'ed",
        text,
        re.I,
    )
    if fatal:
        raise RuntimeError("Fatal event during the map sample")
    return {
        "mode": mode,
        "index": index,
        "profiles": rows,
        "max_handler_ms": max(
            (row["handler_ms"] for row in rows), default=None
        ),
        "map_refresh_present_ms": present[-1] if present else None,
        "note": "Handler logs are rate/threshold limited. Map marker excludes later page labels.",
    }


async def measure(session, args, state, progress, mode, index):
    """Journal each navigation command and save its independent UART window."""
    client, services, _restart, _uptime_key = session
    for name, delay in (("usp_qa_show_home", 3), ("usp_qa_show_map_full", 4)):
        offset = args.uart_log.stat().st_size
        state["operation"] = {
            "kind": "navigation",
            "status": "prepared",
            "service": name,
            "sample": index,
            "mode": mode,
            "uart_offset": offset,
            "started_at": time.time(),
        }
        save(progress, state)
        await client.execute_service(services[name], {})
        state["operation"]["status"] = "sent_once"
        save(progress, state)
        await asyncio.sleep(delay)
    text = uart_since(args.uart_log, offset)
    result = sample_result(text, mode, index)
    prefix = args.output / f"{mode}-{index:02d}"
    prefix.with_suffix(".log").write_text(text)
    save(prefix.with_suffix(".json"), result)
    state["results"].append(result)
    state["operation"]["status"] = "window_recorded"
    save(progress, state)
    logger.info(
        "{} {} handler={} map_present={}",
        mode,
        index,
        result["max_handler_ms"],
        result["map_refresh_present_ms"],
    )


def summary(results):
    """Describe only observed values; missing observations never become zero."""
    report = {}
    for mode in ("cold", "warm"):
        rows = [row for row in results if row["mode"] == mode]
        report[mode] = {"samples": len(rows)}
        for field in ("max_handler_ms", "map_refresh_present_ms"):
            values = sorted(
                row[field] for row in rows if row[field] is not None
            )
            report[mode][field] = {
                "observed": len(values),
                "median": statistics.median(values) if values else None,
                "p95": (
                    values[math.ceil(0.95 * len(values)) - 1]
                    if values
                    else None
                ),
                "maximum": max(values) if values else None,
            }
    return report


async def read_initial_uptime(session):
    """Read the current uptime before deciding whether another boot wait is needed."""
    done = asyncio.Event()
    values = []

    def on_state(state):
        if getattr(state, "key", None) != session[3]:
            return
        value = getattr(state, "state", None)
        if (
            isinstance(value, (float, int))
            and math.isfinite(value)
            and value >= 0
        ):
            values.append(value)
            done.set()

    session[0].subscribe_states(on_state)
    await asyncio.wait_for(done.wait(), 15)
    logger.info("Initial uptime readback {} seconds", values[-1])
    return values[-1]


async def main(args):
    """Run one recoverable series, allowing the boot safety timer to settle."""
    args.output.mkdir(parents=True, exist_ok=True)
    progress = args.output / "benchmark-progress.json"
    if progress.exists():
        raise RuntimeError(
            "Existing journal: inspect it; do not restart the series"
        )
    state = {
        "version": args.expected_version,
        "status": "in_progress",
        "next_sample": args.start_index,
        "operation": None,
        "results": [],
    }
    save(progress, state)
    session = await connect(args)
    initial_uptime = await read_initial_uptime(session)
    state["initial_uptime_seconds"] = initial_uptime
    safe_after = time.monotonic() + max(
        0, args.boot_settle + 10 - initial_uptime
    )
    try:
        for index in range(
            args.start_index, args.start_index + args.cold_reboots
        ):
            state["next_sample"] = index
            state["waiting_for_boot_settle_until"] = time.time() + max(
                0, safe_after - time.monotonic()
            )
            save(progress, state)
            logger.info(
                "Waiting for the successful-boot safety interval before sample {}",
                index,
            )
            await asyncio.sleep(max(0, safe_after - time.monotonic()))
            session, confirmed_at = await restart_once(
                session, args, state, progress
            )
            safe_after = confirmed_at + args.boot_settle
            await measure(session, args, state, progress, "cold", index)
            await measure(session, args, state, progress, "warm", index)
            state["next_sample"] = index + 1
            save(progress, state)
        await session[0].execute_service(session[1]["usp_qa_show_home"], {})
        state["status"] = "completed"
        state["summary"] = summary(state["results"])
        save(progress, state)
        save(args.output / "summary.json", state["summary"])
    finally:
        with suppress(Exception):
            await session[0].disconnect(force=True)


if __name__ == "__main__":
    asyncio.run(main(arguments()))
