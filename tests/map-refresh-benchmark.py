#!/usr/bin/env python3
"""Measure cold and warm map opens; explicit cold runs restart the panel.

Every restart intent is saved before sending. Reusing an output directory
is refused so an interrupted run cannot replay a possibly delivered restart.
"""
from __future__ import annotations

import argparse
import asyncio
from contextlib import suppress
from dataclasses import dataclass, field
import json
import math
import os
from pathlib import Path
import re
import statistics
import sys
import time

from aioesphomeapi import LogLevel
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from history_native_api_common import build_client  # noqa: E402

PROFILE = re.compile(
    r"LVGL_REFRESH_PROFILE seq=(\d+) handler_ms=(\d+) pre_ms=(\d+) "
    r"render_ms=(\d+) post_ms=(\d+) render_seen=(YES|NO)"
)


def arguments():
    """Require an explicit output, version and reboot count."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--cold-reboots", type=int, required=True)
    parser.add_argument("--address", default="192.0.2.208")
    args = parser.parse_args()
    if not 1 <= args.cold_reboots <= 20:
        parser.error("--cold-reboots must be between 1 and 20")
    return args


def save(path, data):
    """Persist a complete record before replacing the previous file."""
    temporary = path.with_suffix(".tmp")
    with temporary.open("w") as stream:
        stream.write(json.dumps(data, indent=2) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


@dataclass
class Session:
    """Keep one API connection, its subscriptions and current uptime."""

    client: object
    services: dict
    restart_key: int
    uptime_key: int
    uptime: float | None = None
    lines: list = field(default_factory=list)
    stop_logs: object = None

    def on_state(self, state):
        """Read only the published uptime needed to confirm a reboot."""
        if getattr(state, "key", None) == self.uptime_key:
            self.uptime = float(state.state)

    def on_log(self, message):
        """Keep diagnostic timing and fault lines without a config dump."""
        value = getattr(message, "message", b"")
        text = value.decode(errors="replace") if isinstance(value, bytes) else str(value)
        if any(word in text for word in (
            "LVGL_REFRESH_PROFILE", "took a long time", "Guru Meditation",
            "watchdog", "assert failed", "panic",
        )):
            self.lines.append(text)

    async def close(self):
        """Release subscriptions even when a restart already closed the API."""
        if callable(self.stop_logs):
            with suppress(Exception):
                self.stop_logs()
        with suppress(Exception):
            await self.client.disconnect()


async def connect(args):
    """Open a read-only API session and confirm its expected identity."""
    client = build_client(ROOT / "firmware/ukraine-status-panel.yaml", args.address)
    try:
        await asyncio.wait_for(client.connect(login=True), timeout=12)
        info = await client.device_info()
        if info.name != "ukraine-status-panel" or info.project_version != args.expected_version:
            raise RuntimeError("Unexpected panel identity or firmware version")
        entities, services = await client.list_entities_services()
        restart = next(entity for entity in entities if entity.name == "Restart")
        uptime = next(entity for entity in entities if entity.name == "Uptime")
        session = Session(client, {item.name: item for item in services}, restart.key, uptime.key)
        client.subscribe_states(session.on_state)
        session.stop_logs = client.subscribe_logs(
            session.on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False
        )
        return session
    except BaseException:
        with suppress(Exception):
            await client.disconnect()
        raise


async def uptime(session, minimum=0):
    """Wait for a valid published uptime, never infer it from a connection."""
    deadline = time.monotonic() + 30
    while session.uptime is None or session.uptime < minimum:
        if time.monotonic() > deadline:
            raise RuntimeError("No valid uptime sample")
        await asyncio.sleep(0.25)
    return session.uptime


async def restart_and_confirm(session, args, state, progress):
    """Send one recorded restart and confirm a lower uptime by readback."""
    before = await uptime(session, minimum=15)
    state["pending_restart"] = {
        "sample": state["next_sample"], "status": "prepared",
        "uptime_before": before,
    }
    save(progress, state)
    session.client.button_command(session.restart_key)
    state["pending_restart"]["status"] = "in_doubt"
    save(progress, state)
    await asyncio.sleep(2)
    await session.close()
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        candidate = None
        try:
            candidate = await connect(args)
            after = await uptime(candidate)
            if after < before - 5:
                state["pending_restart"] = None
                save(progress, state)
                return candidate
        except Exception as error:
            logger.debug("Readback not ready: {}", type(error).__name__)
        if candidate is not None:
            await candidate.close()
        await asyncio.sleep(1)
    raise RuntimeError("Restart delivery is uncertain; do not resend it")


def summarize_lines(lines, mode, sample):
    """Describe the observation window; missing slow logs do not mean zero."""
    records = [match.groups() for line in lines if (match := PROFILE.search(line))]
    handlers = [int(row[1]) for row in records]
    fatal = [line for line in lines if any(word in line.lower() for word in (
        "guru meditation", "watchdog got triggered", "assert failed", "panic'ed"
    ))]
    if fatal:
        raise RuntimeError("Fatal events during map measurement")
    return {
        "mode": mode, "sample": sample, "reported_handler_ms": handlers,
        "max_reported_ms": max(handlers) if handlers else None,
        "slow_profile_threshold_ms": 50, "window_seconds": 3,
        "note": "API log observation window, not a timestamp of final physical pixels",
    }


async def measure(session, mode, sample, output):
    """Return Home before measuring the next map opening for three seconds."""
    await session.client.execute_service(session.services["usp_qa_show_home"], {})
    await asyncio.sleep(2)
    session.lines.clear()
    await session.client.execute_service(session.services["usp_qa_show_map_full"], {})
    await asyncio.sleep(3)
    lines = list(session.lines)
    (output / f"{mode}-{sample:02d}.log").write_text("\n".join(lines) + "\n")
    result = summarize_lines(lines, mode, sample)
    save(output / f"{mode}-{sample:02d}.json", result)
    logger.info("{} sample={} max_reported_ms={}", mode, sample, result["max_reported_ms"])
    return result


def summary(results):
    """Compute statistics only for observed slow handlers, with a clear limit."""
    output = {}
    for mode in ("cold", "warm"):
        rows = [row for row in results if row["mode"] == mode]
        values = sorted(row["max_reported_ms"] for row in rows if row["max_reported_ms"] is not None)
        output[mode] = {
            "samples": len(rows), "samples_with_slow_logs": len(values),
            "median_ms": statistics.median(values) if values else None,
            "p95_ms": values[math.ceil(0.95 * len(values)) - 1] if values else None,
            "maximum_ms": max(values) if values else None,
            "p95_note": "Small sample; slow-log maxima are not full transition times",
        }
    return output


async def main(args):
    """Refuse replay, record each result and finish on Home."""
    args.output.mkdir(parents=True, exist_ok=True)
    progress = args.output / "benchmark-progress.json"
    if progress.exists():
        raise RuntimeError("Existing run must be inspected; refusing automatic replay")
    state = {"version": args.expected_version, "next_sample": 0,
             "status": "in_progress", "pending_restart": None, "results": []}
    save(progress, state)
    session = await connect(args)
    try:
        for sample in range(args.cold_reboots):
            state["next_sample"] = sample
            save(progress, state)
            session = await restart_and_confirm(session, args, state, progress)
            await asyncio.sleep(2)
            for mode in ("cold", "warm"):
                state["results"].append(await measure(session, mode, sample, args.output))
                save(progress, state)
        await session.client.execute_service(session.services["usp_qa_show_home"], {})
        state["status"] = "completed"
        state["summary"] = summary(state["results"])
        save(progress, state)
        save(args.output / "benchmark-summary.json", state["summary"])
    finally:
        await session.close()


if __name__ == "__main__":
    asyncio.run(main(arguments()))
