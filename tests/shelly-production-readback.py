#!/usr/bin/env python3
"""Read production Shelly Pro EM-50 state without changing the panel."""

import argparse
import asyncio
import json
import math
import os
from pathlib import Path

from loguru import logger

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
MEASUREMENTS = {
    f"Shelly CH{channel} {metric}"
    for channel in (0, 1)
    for metric in (
        "Voltage",
        "Current",
        "Active Power",
        "Apparent Power",
        "Power Factor",
        "Frequency",
    )
}
STATUS_NAMES = {
    "Shelly Enabled",
    "Shelly Host",
    "Shelly Port",
    "Shelly Mains Channel",
    "Shelly Status",
    "Shelly Endpoint",
    "Shelly HTTP Status",
}
NAMES = MEASUREMENTS | STATUS_NAMES


def arguments():
    """Parse read-only probe arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel-production.yaml",
    )
    parser.add_argument("--address", default=os.environ.get("USP_ADDRESS"))
    parser.add_argument("--expected-host", default="192.0.2.179")
    parser.add_argument("--expected-port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=20.0)
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


def enabled(value):
    """Normalize the Native API switch state."""
    if isinstance(value, bool):
        return value
    return str(value).upper() in {"ON", "TRUE", "1"}


def qualification(values, host, port):
    """Classify one ordinary Shelly entity snapshot."""
    if str(values.get("Shelly Status")) != "CONNECTED":
        return False, "SHELLY_NOT_CONNECTED"
    if str(values.get("Shelly HTTP Status")) != "200":
        return False, "SHELLY_HTTP_NOT_200"
    if str(values.get("Shelly Endpoint")) != f"{host}:{port}":
        return False, "SHELLY_ENDPOINT_MISMATCH"
    if str(values.get("Shelly Host")) != host:
        return False, "SHELLY_HOST_MISMATCH"
    if not enabled(values.get("Shelly Enabled")):
        return False, "SHELLY_DISABLED"
    if not finite(values.get("Shelly Port")):
        return False, "SHELLY_PORT_INVALID"
    if int(float(values["Shelly Port"])) != port:
        return False, "SHELLY_PORT_MISMATCH"
    if not all(finite(values.get(name)) for name in MEASUREMENTS):
        return False, "SHELLY_MEASUREMENT_INCOMPLETE"
    return True, "VALID_LIVE_SAMPLE"


class ReadOnlyShellyProbe:
    """Capture ordinary Shelly entities from the production Native API."""

    def __init__(self, args):
        self.args = args
        self.names = {}
        self.values = {}
        self.ready = asyncio.Event()

    def on_state(self, state):
        """Capture only approved non-secret Shelly entity states."""
        name = self.names.get(state.key)
        if not name:
            return
        self.values[name] = getattr(state, "state", None)
        if NAMES.issubset(self.values):
            self.ready.set()

    def result(self, info, services):
        """Build a strict-JSON-compatible read-only snapshot."""
        qualified, reason = qualification(
            self.values,
            self.args.expected_host,
            self.args.expected_port,
        )
        values = {name: self.values.get(name) for name in sorted(NAMES)}
        for name in MEASUREMENTS | {"Shelly Port"}:
            if not finite(values.get(name)):
                values[name] = None
        return {
            "version": str(info.project_version),
            "build": str(info.compilation_time),
            "services": len(services),
            "values": values,
            "qualified": qualified,
            "reason": reason,
            "custom_actions_called": False,
        }

    def save(self, result):
        """Persist one redacted production snapshot."""
        self.args.output.parent.mkdir(parents=True, exist_ok=True)
        self.args.output.write_text(json.dumps(result, indent=2) + "\n")
        status = "PASS" if result["qualified"] else "FAIL"
        logger.info("SHELLY_READBACK={} reason={}", status, result["reason"])

    async def run(self):
        """Connect, subscribe to ordinary entities, and save one snapshot."""
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
                raise RuntimeError(f"Missing Shelly entities: {sorted(missing)}")
            client.subscribe_states(self.on_state)
            try:
                await asyncio.wait_for(self.ready.wait(), timeout=self.args.timeout)
            except asyncio.TimeoutError:
                logger.warning("Shelly capture timed out; saving partial snapshot")
            self.save(self.result(info, services))
        finally:
            await client.disconnect()


def main():
    """Run the production-safe read-only Shelly probe."""
    asyncio.run(ReadOnlyShellyProbe(arguments()).run())


if __name__ == "__main__":
    main()
