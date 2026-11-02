#!/usr/bin/env python3
"""Record real GT911 and LVGL evidence without changing navigation settings."""

import argparse
import asyncio
import inspect
import json
from pathlib import Path
import re
import time

from loguru import logger

from hardware_probe_journal import ProbeJournal
from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware/ukraine-status-panel.yaml"


def arguments():
    """Require fresh output and an independently running serial recorder."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--uart-log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--duration", type=int, default=180)
    args = parser.parse_args()
    if not 1 <= args.duration <= 180:
        parser.error("--duration must be between 1 and 180 seconds")
    if not args.uart_log.is_file() or args.output.exists():
        parser.error("Use an active UART log and a fresh output directory")
    return args


def uart_since(path, offset):
    """Read only newly received serial bytes and remove terminal colours."""
    with path.open("rb") as stream:
        stream.seek(offset)
        text = stream.read().decode("utf-8", "replace")
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


async def call(client, service, values):
    """Support the installed Native API client without resending actions."""
    result = client.execute_service(service, values)
    if inspect.isawaitable(result):
        await result


async def confirm_armed(args, offset):
    """Require an independent UART acknowledgement of the bounded trace."""
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        if "TOUCH_TRACE stage=ARM" in uart_since(args.uart_log, offset):
            logger.info("TRACE_ARMED duration={}s", args.duration)
            return True
        await asyncio.sleep(0.1)
    raise TimeoutError("Trace acknowledgement not received")


def save_evidence(args, offset, version):
    """Keep the physical and synthetic paths distinguishable in the report."""
    lines = [
        line for line in uart_since(args.uart_log, offset).splitlines()
        if any(tag in line for tag in (
            "TOUCH_TRACE", "LCD_ESCAPE", "LCD_SWIPE", "LCD_MODE_STATE"
        ))
    ]
    physical = sum(
        "stage=LVGL_PRESS qa=0" in line for line in lines
    )
    result = {
        "version": version,
        "duration_seconds": args.duration,
        "physical_presses": physical,
        "status": "recorded" if physical else "physical_input_not_observed",
        "physical_acceptance": "requires_manual_result_and_trace_analysis",
        "settings_changed": False,
        "events": lines,
    }
    (args.output / "touch-events.log").write_text("\n".join(lines) + "\n")
    (args.output / "summary.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    )
    logger.info("TRACE_RECORDED physical_presses={} status={}",
                physical, result["status"])


async def capture(args):
    """Arm once, keep serial evidence, and leave preferences unchanged."""
    args.output.mkdir(parents=True)
    client = build_client(CONFIG, args.address)
    await client.connect(login=True)
    try:
        await verify_device(client, CONFIG)
        info = await client.device_info()
        _, services = await client.list_entities_services()
        service = next(s for s in services
                       if s.name == "usp_qa_navigation_trace")
        journal = ProbeJournal(args.output / "journal.json", {})
        offset = args.uart_log.stat().st_size
        await journal.once(
            "trace:arm", args.duration,
            lambda: call(client, service, {"seconds": args.duration}),
            lambda: confirm_armed(args, offset),
        )
        journal.finish(True)
        await asyncio.sleep(args.duration + 1)
        save_evidence(args, offset, str(info.project_version))
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(capture(arguments()))
