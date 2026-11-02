#!/usr/bin/env python3
"""Read production SHT41/SHT45 state without changing the panel."""

import argparse
import asyncio
import json
import math
import os
from pathlib import Path

from loguru import logger

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
NAMES = {
    "SHT4x Status",
    "Environment Temperature",
    "Environment Humidity",
}


def arguments():
    """Parse read-only probe arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel-production.yaml",
    )
    parser.add_argument("--address", default=os.environ.get("USP_ADDRESS"))
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.address:
        parser.error("--address or USP_ADDRESS is required")
    if args.output.exists():
        parser.error("Evidence exists; choose a new output path")
    return args


def finite(value):
    """Return True only for a finite numeric sensor sample."""
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def qualification(values):
    """Classify a production SHT snapshot without device-side actions."""
    status = str(values.get("SHT4x Status") or "UNKNOWN")
    temperature = values.get("Environment Temperature")
    humidity = values.get("Environment Humidity")
    valid = status == "CONNECTED" and finite(temperature) and finite(humidity)
    if valid:
        return True, "VALID_SAMPLE"
    if status == "NOT DETECTED":
        return False, "BLOCKED_EXTERNAL_SHT"
    if status == "STARTING":
        return False, "SHT_STARTING_OR_NO_VALID_SAMPLE"
    return False, "SHT_STATE_INCOMPLETE"


class ReadOnlyShtProbe:
    """Capture ordinary SHT entities from the production Native API."""

    def __init__(self, args):
        self.args = args
        self.names = {}
        self.values = {}
        self.ready = asyncio.Event()

    def on_state(self, state):
        """Capture only the three approved SHT entity states."""
        name = self.names.get(state.key)
        if not name:
            return
        self.values[name] = getattr(state, "state", None)
        status = self.values.get("SHT4x Status")
        if status == "NOT DETECTED" or NAMES.issubset(self.values):
            self.ready.set()

    def save(self, info, services):
        """Persist a redacted read-only hardware snapshot."""
        qualified, reason = qualification(self.values)
        values = {name: self.values.get(name) for name in sorted(NAMES)}
        for name in ("Environment Temperature", "Environment Humidity"):
            if not finite(values[name]):
                values[name] = None
        result = {
            "version": str(info.project_version),
            "build": str(info.compilation_time),
            "services": len(services),
            "values": values,
            "qualified": qualified,
            "reason": reason,
            "custom_actions_called": False,
        }
        self.args.output.parent.mkdir(parents=True, exist_ok=True)
        self.args.output.write_text(json.dumps(result, indent=2) + "\n")
        status = "PASS" if qualified else "BLOCKED"
        logger.info("SHT_READBACK={} reason={}", status, reason)

    async def run(self):
        """Connect, subscribe to ordinary entities, save one snapshot."""
        client = build_client(self.args.config, self.args.address)
        await client.connect(login=True)
        try:
            await verify_device(client, self.args.config)
            info = await client.device_info()
            entities, services = await client.list_entities_services()
            self.names = {
                item.key: item.name for item in entities if item.name in NAMES
            }
            missing = NAMES - set(self.names.values())
            if missing:
                raise RuntimeError(f"Missing SHT entities: {sorted(missing)}")
            client.subscribe_states(self.on_state)
            try:
                await asyncio.wait_for(self.ready.wait(), timeout=self.args.timeout)
            except asyncio.TimeoutError:
                logger.warning("SHT state capture timed out; saving partial snapshot")
            self.save(info, services)
        finally:
            await client.disconnect()


def main():
    """Run the production-safe read-only SHT probe."""
    asyncio.run(ReadOnlyShtProbe(arguments()).run())


if __name__ == "__main__":
    main()
