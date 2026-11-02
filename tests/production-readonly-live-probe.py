#!/usr/bin/env python3
"""Read ordinary production state without calling any custom Native API action."""

import argparse
import asyncio
import json
import os
from pathlib import Path

from loguru import logger

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = {
    "LCD Mode",
    "UI Language",
    "Selected Region",
    "Alert Map Source",
    "Alert Live State",
    "Panel Sounds Enabled",
    "Alarm Status",
    "Outage Group",
}
DIAGNOSTICS = {
    "Firmware Version",
    "Free Heap",
    "Free PSRAM",
    "Alert Data Age",
    "Alert Accepted Frames",
    "Alert Rejected Frames",
    "Alert Live Revision",
    "Web Security Status",
}


class ReadOnlyProbe:
    """Subscribe only to explicitly non-secret settings and diagnostics."""

    def __init__(self, args):
        """Keep connection creation inside the running asynchronous capture."""
        self.args = args
        self.values = {}
        self.names = {}
        self.ready = asyncio.Event()

    def on_state(self, value):
        """Ignore all states outside the non-secret whitelist."""
        if value.key not in self.names:
            return
        self.values[self.names[value.key]] = getattr(value, "state", None)
        required = {
            "LCD Mode",
            "UI Language",
            "Alert Map Source",
            "Alert Live State",
            "Free Heap",
            "Firmware Version",
            "Outage Group",
        }
        if required.issubset(self.values) and self.values["Alert Live State"] == "LIVE":
            self.ready.set()

    def save(self, info, services):
        """Write a complete snapshot without exposing passwords or keys."""
        settings = {
            name: value
            for name, value in self.values.items()
            if name in SETTINGS or name.startswith("Alarm ") or "Palette" in name
        }
        result = dict(
            version=info.project_version,
            build=info.compilation_time,
            settings=settings,
            services=len(services),
            diagnostics={k: v for k, v in self.values.items() if k in DIAGNOSTICS},
            custom_actions_called=False,
        )
        assert not services, "Ordinary production exposes custom actions"
        assert settings["Alert Map Source"] == "NEPTUN"
        assert settings["Alert Live State"] == "LIVE"
        self.args.output.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        )
        logger.info("PRODUCTION_READONLY=PASS services0 sourceNEPTUN LIVE")

    async def run(self):
        """Read identity and ordinary states; never execute device actions."""
        client = build_client(self.args.config, self.args.address)
        await client.connect(login=True)
        try:
            await verify_device(client, self.args.config)
            info = await client.device_info()
            assert info.name == "ukraine-status-panel"
            entities, services = await client.list_entities_services()
            self.names = {
                entity.key: entity.name
                for entity in entities
                if entity.name in SETTINGS | DIAGNOSTICS
                or entity.name.startswith("Alarm ")
                or "Palette" in entity.name
            }
            client.subscribe_states(self.on_state)
            await asyncio.wait_for(self.ready.wait(), timeout=30)
            await asyncio.sleep(2)
            self.save(info, services)
        finally:
            await client.disconnect()


def main():
    """Require a new output file and default to the generated ordinary profile."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel-production.yaml",
    )
    parser.add_argument(
        "--address", default=os.environ.get("USP_ADDRESS", "192.0.2.208")
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Evidence exists; inspect it before choosing a new output")
    asyncio.run(ReadOnlyProbe(args).run())


if __name__ == "__main__":
    main()
