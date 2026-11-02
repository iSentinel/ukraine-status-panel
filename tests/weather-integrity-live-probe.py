#!/usr/bin/env python3
"""Read live weather evidence without changing the selected city or settings."""

from __future__ import annotations

import argparse
import asyncio
import os
from pathlib import Path
import re

from aioesphomeapi import LogLevel
from loguru import logger

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware/ukraine-status-panel.yaml"
ANSI = re.compile(r"\x1b\[[0-9;]*m")
SNAPSHOT = re.compile(
    r"WEATHER city=(.*?) valid=(\S+) stale=(\S+) fetch_ok=(\S+).*?"
    r"days=(\d+) seq=(\d+)"
)


def arguments() -> argparse.Namespace:
    """Describe the bounded live check and its network refresh."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--address",
        default=os.environ.get("USP_ADDRESS", "ukraine-status-panel.local"),
    )
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    if not 5 <= args.timeout <= 120:
        parser.error("--timeout must be between 5 and 120 seconds")
    return args


def collect_weather(lines: list[str], message: object) -> None:
    """Keep weather diagnostics and log the received text without credentials."""
    raw = getattr(message, "message", b"")
    text = raw.decode(errors="replace") if isinstance(raw, bytes) else str(raw)
    text = ANSI.sub("", text)
    if "usp.weather" in text:
        lines.append(text)
        logger.info("{}", text)


async def await_snapshot(client, services, lines, timeout: float) -> None:
    """Request the same city's update and require genuine current/forecast data."""
    await client.execute_service(services["usp_weather_refresh"], {})
    deadline = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < deadline:
        marker = len(lines)
        await client.execute_service(services["usp_weather_dump"], {})
        await asyncio.sleep(2)
        for line in lines[marker:]:
            match = SNAPSHOT.search(line)
            if match is None:
                continue
            city, valid, stale, fetch_ok, days, sequence = match.groups()
            if city and (valid, stale, fetch_ok) == ("YES", "NO", "YES"):
                if int(days) > 0 and int(sequence) > 0:
                    logger.info(
                        "WEATHER_LIVE=PASS city={} days={}", city, days
                    )
                    return
    raise RuntimeError("Fresh current weather and forecast were not observed")


async def main(args: argparse.Namespace) -> None:
    """Verify identity, observe weather and restore Home after visiting its pages."""
    client = build_client(CONFIG, args.address)
    await client.connect(login=True)
    stop = None
    try:
        await verify_device(client, CONFIG)
        _, discovered = await client.list_entities_services()
        services = {str(service.name): service for service in discovered}
        needed = (
            "usp_weather_refresh",
            "usp_weather_dump",
            "usp_qa_show_weather",
            "usp_qa_show_weather_full",
            "usp_qa_show_home",
        )
        missing = set(needed) - services.keys()
        if missing:
            raise RuntimeError(f"Missing services: {sorted(missing)}")
        lines: list[str] = []
        stop = client.subscribe_logs(
            lambda message: collect_weather(lines, message),
            log_level=LogLevel.LOG_LEVEL_INFO,
            dump_config=False,
        )
        await await_snapshot(client, services, lines, args.timeout)
        for name in ("usp_qa_show_weather", "usp_qa_show_weather_full"):
            await client.execute_service(services[name], {})
            await asyncio.sleep(1)
        await client.execute_service(services["usp_qa_show_home"], {})
        logger.info(
            "Weather pages visited through Native API; touch not tested"
        )
    finally:
        if stop is not None:
            stop()
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main(arguments()))
