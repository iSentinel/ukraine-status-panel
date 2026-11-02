#!/usr/bin/env python3
"""Verify empty secret states after the current write-only firmware is installed."""

import argparse
import asyncio
import json
from pathlib import Path

from loguru import logger

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {"SvitloBot Channel Key", "Network Password"}


class EmptyStateProbe:
    """Retain only empty-state booleans; never save or log a field value."""

    def __init__(self, args):
        """Keep client construction inside the active event loop."""
        self.args = args
        self.names = {}
        self.empty = {}
        self.ready = asyncio.Event()

    def on_state(self, value):
        """Inspect only required public states and store no credential content."""
        if value.key not in self.names:
            return
        self.empty[self.names[value.key]] = value.state == ""
        if REQUIRED.issubset(self.empty):
            self.ready.set()

    async def run(self):
        """Verify identity before subscribing; perform no writes or actions."""
        client = build_client(self.args.config, self.args.address)
        await client.connect(login=True)
        try:
            await verify_device(client, self.args.config)
            entities, services = await client.list_entities_services()
            assert not services, "Ordinary production must expose no custom actions"
            self.names = {
                item.key: item.name for item in entities if item.name in REQUIRED
            }
            assert set(self.names.values()) == REQUIRED
            client.subscribe_states(self.on_state)
            await asyncio.wait_for(self.ready.wait(), timeout=15)
            await asyncio.sleep(1)
            assert all(self.empty.values()), "A required public state is not empty"
            result = dict(
                status="PASS",
                public_states_empty=self.empty,
                settings_written=False,
                custom_actions_called=False,
                credential_values_saved=False,
            )
            self.args.output.write_text(json.dumps(result, indent=2) + "\n")
            logger.info("WRITE_ONLY_SECRET_STATES=PASS fields2 values not saved")
        finally:
            await client.disconnect()


def main():
    """Require new evidence and default to the generated ordinary profile."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel-production.yaml",
    )
    parser.add_argument("--address", default="192.0.2.208")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Evidence exists; inspect it before choosing another path")
    asyncio.run(EmptyStateProbe(args).run())


if __name__ == "__main__":
    main()
