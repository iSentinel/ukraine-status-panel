#!/usr/bin/env python3
"""Read the panel identity without changing settings or restarting it."""

import argparse
import asyncio
import json
import re
import time
from pathlib import Path

from loguru import logger

from history_native_api_common import build_client

ROOT = Path(__file__).resolve().parents[1]


def arguments():
    """Select the existing firmware configuration and expected device."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--expected", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--uart-log", type=Path)
    return parser.parse_args()


async def read_navigation(client, path):
    """Read mode and active page through a fresh independent UART reply."""
    _, services = await client.list_entities_services()
    action = next(s for s in services if s.name == "usp_qa_dump_navigation_mode")
    offset = path.stat().st_size
    await client.execute_service(action, {})
    deadline = time.monotonic() + 6
    pattern = (
        r"LCD_MODE_STATE mode=(\d+) page=(\d+) nav=(\S+) menu=(\S+) "
        r"region=(\d+) consumed=(\S+)"
    )
    while time.monotonic() < deadline:
        with path.open("rb") as stream:
            stream.seek(offset)
            text = stream.read().decode("utf-8", "replace")
        text = re.sub(r"\x1b\[[0-9;]*m", "", text)
        match = re.search(pattern, text)
        if match:
            keys = ("mode", "page", "nav", "menu", "region", "consumed")
            return dict(zip(keys, match.groups()))
        await asyncio.sleep(0.1)
    raise TimeoutError("Navigation state not observed in UART")


async def read_identity(args):
    """Save only the device name, firmware version and compilation time."""
    client = build_client(
        ROOT / "firmware/ukraine-status-panel.yaml", args.address
    )
    await client.connect(login=True)
    try:
        info = await client.device_info()
        result = {
            "name": str(info.name),
            "version": str(info.project_version),
            "build": str(info.compilation_time),
        }
        if result["version"] != args.expected:
            raise RuntimeError(f"Unexpected device identity: {result}")
        if args.uart_log:
            result["navigation"] = await read_navigation(client, args.uart_log)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
        logger.info("DEVICE_IDENTITY=PASS {}", result)
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(read_identity(arguments()))
