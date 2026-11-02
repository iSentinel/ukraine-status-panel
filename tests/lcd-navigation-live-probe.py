#!/usr/bin/env python3
"""Activate the real LCD menu callbacks and read back the active page."""

import argparse
import asyncio
import os
from pathlib import Path
import re
import time

from loguru import logger

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware/ukraine-status-panel.yaml"
STATE = re.compile(
    r"LCD_NAV page=(\d+) visible=(\S+) buttons=7 weather=([^\r\n\x1b]+)"
)


def arguments():
    """Describe the UART evidence and connection used by this bounded check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", default=os.environ.get("USP_ADDRESS"))
    parser.add_argument("--uart-log", type=Path, required=True)
    args = parser.parse_args()
    if not args.address:
        parser.error("--address or USP_ADDRESS is required")
    if not args.uart_log.is_file():
        parser.error("--uart-log must refer to an active capture")
    return args


async def snapshot(client, services, path):
    """Read an independent UART record written after the dump command."""
    offset = path.stat().st_size
    await client.execute_service(services["usp_qa_dump_lcd_navigation"], {})
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        with path.open("rb") as stream:
            stream.seek(offset)
            match = STATE.search(stream.read().decode("utf-8", "replace"))
        if match:
            page, visible, label = match.groups()
            return int(page), visible, label.strip()
        await asyncio.sleep(0.1)
    raise TimeoutError("LCD menu state was not observed in UART")


async def check_destinations(client, services, path):
    """Follow every actual menu callback and require a visible Weather entry."""
    await client.execute_service(services["usp_qa_show_home"], {})
    await asyncio.sleep(1.5)
    for page in (1, 2, 3, 4, 9, 5, 0, 9, 0):
        await client.execute_service(
            services["usp_qa_navigation_click"], {"page": page}
        )
        await asyncio.sleep(0.2)
        actual, visible, weather = await snapshot(client, services, path)
        if (
            actual != page
            or visible != "YES"
            or weather not in ("Погода", "Weather")
        ):
            raise RuntimeError(f"Wrong menu state: {actual, visible, weather}")
        logger.info("LCD_MENU_ROUTE=PASS page={} weather={}", actual, weather)
    logger.info(
        "LCD_MENU_HARDWARE=PASS callbacks=9; physical finger not tested"
    )


async def main(args):
    """Verify the installed build and test menu actions without changing settings."""
    client = build_client(CONFIG, args.address)
    await client.connect(login=True)
    try:
        await verify_device(client, CONFIG)
        _, discovered = await client.list_entities_services()
        services = {str(item.name): item for item in discovered}
        needed = {
            "usp_qa_dump_lcd_navigation",
            "usp_qa_navigation_click",
            "usp_qa_show_home",
        }
        if needed - services.keys():
            raise RuntimeError("Required LCD navigation services are missing")
        await check_destinations(client, services, args.uart_log)
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main(arguments()))
