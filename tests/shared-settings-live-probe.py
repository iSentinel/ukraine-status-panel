#!/usr/bin/env python3
"""Check shared settings with a durable original-value and restoration journal."""

import argparse
import asyncio
import os
from pathlib import Path
import re
import time

from aioesphomeapi import LogLevel
from loguru import logger

from hardware_probe_journal import ProbeJournal
from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(
    os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml")
)
SNAPSHOT = re.compile(
    r"SETTINGS134 language=(\d+) palette=(\d+) ui_region=(\d+) "
    r"stored_region=(\d+) outage=(\d+) backlight=([0-9.]+) "
    r"light=([0-9.]+) slider=(-?\d+)"
)


def arguments():
    """Allow a live UART file and an explicit fresh journal path."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--uart-log", type=Path, default=os.environ.get("USP_UART_LOG")
    )
    parser.add_argument(
        "--journal", type=Path, default=os.environ.get("USP_SETTINGS_JOURNAL")
    )
    args = parser.parse_args()
    if args.journal is None:
        args.journal = (
            ROOT / ".work-state" / f"shared-settings-{time.time_ns()}.json"
        )
    if args.journal.exists():
        parser.error("Existing journal: inspect it before continuing")
    if args.uart_log is not None and not args.uart_log.is_file():
        parser.error("Start an independent UART recorder first")
    return args


class SharedSettingsProbe:
    """Use one settings path and return every attempted change once."""

    def __init__(self, args):
        """Keep callbacks, identity and journal state within this test instance."""
        self.args = args
        self.client = build_client(
            CONFIG, os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
        )
        self.services = {}
        self.light_key = None
        self.light_state = None
        self.lines = []
        self.warnings = []
        self.journal = None
        self.stop_logs = None

    def on_log(self, message):
        """Keep just the records needed by the assertions."""
        value = getattr(message, "message", b"")
        text = (
            value.decode("utf-8", "replace")
            if isinstance(value, bytes)
            else str(value)
        )
        if "SETTINGS134 " in text:
            self.lines.append(text)
            logger.info("{}", text)
        if "lvgl took a long time" in text:
            self.warnings.append(text)

    def on_state(self, state):
        """Remember the actual light on/off state as well as its brightness."""
        if getattr(state, "key", None) == self.light_key:
            self.light_state = state

    async def open(self):
        """Confirm the panel before any test mutation."""
        await asyncio.wait_for(self.client.connect(login=True), 15)
        await verify_device(self.client, CONFIG)
        entities, services = await self.client.list_entities_services()
        self.services = {item.name: item for item in services}
        self.light_key = next(
            item.key for item in entities if item.name == "Panel Backlight"
        )
        self.client.subscribe_states(self.on_state)
        self.stop_logs = self.client.subscribe_logs(
            self.on_log, log_level=LogLevel.LOG_LEVEL_INFO, dump_config=False
        )

    def read_since(self, offset):
        """Read appended UART bytes or the filtered API log fallback."""
        if self.args.uart_log is None:
            return "\n".join(self.lines[offset:])
        with self.args.uart_log.open("rb") as stream:
            stream.seek(offset)
            return stream.read().decode("utf-8", "replace")

    async def snapshot(self):
        """Send one read-only dump and match a subsequent independent record."""
        offset = (
            self.args.uart_log.stat().st_size
            if self.args.uart_log
            else len(self.lines)
        )
        await self.client.execute_service(
            self.services["usp_qa_dump_shared_settings_134"], {}
        )
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            match = SNAPSHOT.search(self.read_since(offset))
            if match:
                values = match.groups()
                return [
                    *(int(value) for value in values[:5]),
                    *(float(value) for value in values[5:7]),
                    int(values[7]),
                ]
            await asyncio.sleep(0.1)
        raise TimeoutError(
            "SETTINGS134 snapshot missing; inspect the restoration journal"
        )

    async def record_original(self):
        """Save original settings and light state before the first write."""
        initial = await self.snapshot()
        deadline = time.monotonic() + 10
        while self.light_state is None and time.monotonic() < deadline:
            await asyncio.sleep(0.1)
        if self.light_state is None:
            raise RuntimeError(
                "No original light state; test changes are forbidden"
            )
        original = {
            "snapshot": initial,
            "group": initial[4],
            "brightness": initial[6],
            "light_state": bool(self.light_state.state),
            "light_brightness": float(self.light_state.brightness),
        }
        self.journal = ProbeJournal(self.args.journal, original)
        logger.info("Original settings saved to {}", self.args.journal)

    async def brightness(self, restore=False):
        """Change or restore brightness once, preserving the original on/off state."""
        original = self.journal.state["initial"]
        target = 35.0 if original["brightness"] >= 55 else 75.0
        brightness = original["light_brightness"] if restore else target / 100
        state = original["light_state"] if restore else True
        expected = original["snapshot"][5:8] if restore else [target] * 3
        warning_count = len(self.warnings)

        async def verify():
            await asyncio.sleep(1.3)
            snapshot = await self.snapshot()
            if snapshot[2] != snapshot[3]:
                raise RuntimeError("UI and alert region disagree")
            if any(
                abs(actual - wanted) > 1.1
                for actual, wanted in zip(snapshot[5:8], expected)
            ):
                raise RuntimeError("Backlight/global/slider mismatch")
            if bool(self.light_state.state) != state:
                raise RuntimeError("Backlight on/off state mismatch")
            if not restore and len(self.warnings) != warning_count:
                raise RuntimeError(
                    "Brightness change caused an LVGL long operation"
                )
            return snapshot

        await self.journal.once(
            "restore:brightness" if restore else "change:brightness",
            {"state": state, "brightness": brightness},
            lambda: self.client.light_command(
                self.light_key,
                state=state,
                brightness=brightness,
                transition_length=0.0,
            ),
            verify,
        )
        logger.info(
            "{}=PASS",
            "BACKLIGHT_RESTORE" if restore else "EXTERNAL_BACKLIGHT_SYNC",
        )

    async def group(self, restore=False):
        """Change or restore the group once and require readback."""
        original = self.journal.state["initial"]["group"]
        target = original if restore else (11 if original != 11 else 10)

        async def verify():
            await asyncio.sleep(0.8)
            snapshot = await self.snapshot()
            if snapshot[4] != target:
                raise RuntimeError("Outage group readback mismatch")
            return snapshot

        await self.journal.once(
            "restore:group" if restore else "change:group",
            target,
            lambda: self.client.execute_service(
                self.services["usp_outage_set_group"], {"group": target}
            ),
            verify,
        )
        logger.info(
            "{}=PASS",
            "OUTAGE_GROUP_RESTORE" if restore else "OUTAGE_GROUP_CHANGE",
        )

    async def restore_pending(self):
        """Attempt each distinct missing restore once, even after an assertion fails."""
        if self.journal is None:
            return
        for field in ("group", "brightness"):
            if not self.journal.needs_restore(field):
                continue
            try:
                if not self.client.is_connected:
                    await asyncio.wait_for(self.client.connect(login=True), 15)
                    await verify_device(self.client, CONFIG)
                    self.client.subscribe_states(self.on_state)
                await getattr(self, field)(restore=True)
            except Exception as error:
                logger.error(
                    "Restore {} unconfirmed: {}; inspect {}",
                    field,
                    type(error).__name__,
                    self.args.journal,
                )

    async def run(self):
        """Keep restoration and disconnect active for all failure paths."""
        successful = False
        try:
            await self.open()
            await self.client.execute_service(
                self.services["usp_qa_show_settings"], {}
            )
            await asyncio.sleep(1)
            await self.record_original()
            await self.brightness()
            await self.brightness(restore=True)
            await self.group()
            await self.group(restore=True)
            await self.client.execute_service(
                self.services["usp_qa_show_home"], {}
            )
            successful = True
            logger.info("SHARED_SETTINGS_LIVE=PASS")
        finally:
            await self.restore_pending()
            if self.journal is not None:
                self.journal.finish(successful)
            if self.stop_logs is not None:
                self.stop_logs()
            await self.client.disconnect(force=True)


async def main() -> None:
    """Create the API client after the asynchronous event loop starts."""
    await SharedSettingsProbe(arguments()).run()


if __name__ == "__main__":
    asyncio.run(main())
