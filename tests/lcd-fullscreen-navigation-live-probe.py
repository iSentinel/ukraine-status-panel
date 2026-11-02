#!/usr/bin/env python3
"""Exercise real LVGL pointer processing and persisted LCD navigation modes."""

import argparse
import asyncio
import inspect
from pathlib import Path
import re
import time

from loguru import logger

from hardware_probe_journal import ProbeJournal
from history_native_api_common import (
    build_client,
    expected_version,
    verify_device,
)

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware/ukraine-status-panel.yaml"
STATE = re.compile(
    r"LCD_MODE_STATE mode=(\d+) page=(\d+) nav=(\S+) menu=(\S+) "
    r"region=(\d+) consumed=(\S+)"
)


def arguments():
    """Require a live UART capture and a fresh durable restoration journal."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--uart-log", type=Path, required=True)
    parser.add_argument("--journal", type=Path, required=True)
    parser.add_argument("--reboots", action="store_true")
    args = parser.parse_args()
    if not args.uart_log.is_file() or args.journal.exists():
        parser.error(
            "An active UART capture and a new journal path are required"
        )
    return args


class NavigationProbe:
    """Keep original mode, independent readback and non-replayable commands."""

    def __init__(self, args):
        """Create the API client inside the running asynchronous main loop."""
        self.args = args
        self.client = build_client(CONFIG, args.address)
        self.services = {}
        self.mode_key = None
        self.restart_key = None
        self.original = None
        self.journal = None

    async def connect(self):
        """Verify the exact installed build and discover supported actions."""
        await self.client.connect(login=True)
        await verify_device(self.client, CONFIG)
        entities, services = await self.client.list_entities_services()
        self.services = {str(item.name): item for item in services}
        self.mode_key = next(
            item.key for item in entities if item.name == "LCD Mode"
        )
        self.restart_key = next(
            item.key for item in entities if item.name == "Restart"
        )

    async def call(self, name, values=None):
        """Send an action once; the caller checks its independent outcome."""
        result = self.client.execute_service(self.services[name], values or {})
        if inspect.isawaitable(result):
            await result

    def uart_since(self, offset):
        """Read appended serial evidence without replacing the recorder."""
        with self.args.uart_log.open("rb") as stream:
            stream.seek(offset)
            text = stream.read().decode("utf-8", "replace")
        return re.sub(r"\x1b\[[0-9;]*m", "", text)

    async def snapshot(self):
        """Require a fresh serial response to the read-only mode dump."""
        offset = self.args.uart_log.stat().st_size
        await self.call("usp_qa_dump_navigation_mode")
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            match = STATE.search(self.uart_since(offset))
            if match:
                mode, page, nav, menu, region, consumed = match.groups()
                return dict(
                    mode=int(mode),
                    page=int(page),
                    nav=nav,
                    menu=menu,
                    region=int(region),
                    consumed=consumed,
                )
            await asyncio.sleep(0.1)
        raise TimeoutError("Navigation dump not observed in UART")

    async def require(self, **wanted):
        """Check requested fields and preserve the user's selected region."""
        state = await self.snapshot()
        for key, value in wanted.items():
            if state[key] != value:
                raise RuntimeError(f"Expected {wanted}, received {state}")
        if self.original and state["region"] != self.original["region"]:
            raise RuntimeError(f"A gesture selected another region: {state}")
        logger.info("NAV_STATE {}", state)
        return state

    async def set_mode(self, mode, operation):
        """Change one persisted setting through the public select backend."""

        async def verify():
            await asyncio.sleep(0.25)
            return await self.require(mode=mode, nav="NO" if mode else "YES")

        await self.journal.once(
            operation,
            mode,
            lambda: self.client.select_command(
                self.mode_key, "Fullscreen" if mode else "Buttons"
            ),
            verify,
        )

    async def pointer(self, x, y, pressed):
        """Feed the explicit QA pointer; normal LVGL hit testing still applies."""
        await self.call(
            "usp_qa_navigation_touch", {"x": x, "y": y, "pressed": pressed}
        )

    async def drag(self, start, end, duration=0.24):
        """Start after cold page rendering, then deliver one bounded stroke."""
        # The page ID changes before the first frame is fully rendered.
        await asyncio.sleep(1.0)
        started = time.monotonic()
        await self.pointer(*start, True)
        # Native API service calls are fire-and-forget from the client side.
        # Require a fresh UART state after PRESSED so the device starts the
        # gesture timer before later coordinates can queue behind it.
        await self.snapshot()
        for index in range(1, 4):
            await asyncio.sleep(duration / 4)
            x = round(start[0] + (end[0] - start[0]) * index / 4)
            y = round(start[1] + (end[1] - start[1]) * index / 4)
            await self.pointer(x, y, True)
        await asyncio.sleep(duration / 4)
        await self.pointer(*end, False)
        logger.info(
            "POINTER_STROKE wall_ms={} requested_ms={}",
            round((time.monotonic() - started) * 1000), round(duration * 1000),
        )
        await asyncio.sleep(0.35)

    async def check_chain(self):
        """Cycle through every main page in both directions without map selection."""
        await self.call("usp_qa_show_home")
        await asyncio.sleep(1)
        for page in (6, 7, 3, 4, 9, 0):
            await self.drag((700, 300), (350, 300))
            await self.require(mode=1, page=page, nav="NO", menu="NO")
        for page in (9, 4, 3, 7, 6, 0):
            await self.drag((350, 300), (700, 300))
            await self.require(mode=1, page=page, nav="NO", menu="NO")
        logger.info("SWIPE_CHAIN=PASS transitions=12 boundaries=cyclic")

    async def check_rejected_movements(self):
        """Do not turn short, diagonal or slow map drags into navigation/taps."""
        await self.drag((700, 300), (350, 300))
        await self.require(page=6)
        for end, duration in (
            ((650, 300), 0.24),
            ((600, 390), 0.24),
            ((350, 300), 1.0),
        ):
            await self.drag((700, 300), end, duration)
            await self.require(page=6, consumed="YES", menu="NO")
        await self.pointer(25, 75, True)
        await self.snapshot()
        await asyncio.sleep(0.15)
        await self.pointer(25, 75, False)
        await asyncio.sleep(0.25)
        await self.require(page=6, consumed="NO")
        logger.info("MAP_INPUT=PASS drag_suppressed=3 ordinary_tap=1")

    async def check_settings_button(self):
        """Swipe away from Settings background but protect its controls."""
        await self.call("usp_qa_show_settings")
        await asyncio.sleep(0.5)
        await self.drag((560, 45), (300, 45))
        await self.require(mode=1, page=0, menu="NO")

        await self.call("usp_qa_show_settings")
        await asyncio.sleep(0.5)
        await self.drag((560, 45), (820, 45))
        await self.require(mode=1, page=9, menu="NO")

        await self.call("usp_qa_show_settings")
        await asyncio.sleep(0.5)
        await self.drag((900, 35), (650, 35))
        await self.require(mode=1, page=5, consumed="YES", menu="NO")
        logger.info(
            "SETTINGS_INPUT=PASS background_next=home background_previous=clock "
            "mode_button_drag_blocked=1"
        )

    async def check_local_escape(self):
        """Open the local menu while the API client is disconnected."""
        await self.call("usp_qa_show_home")
        await asyncio.sleep(0.5)
        offset = self.args.uart_log.stat().st_size
        await self.pointer(150, 30, True)
        await self.snapshot()
        await self.client.disconnect(force=True)
        await asyncio.sleep(1.8)
        if "LCD_ESCAPE menu=OPEN" not in self.uart_since(offset):
            raise RuntimeError("Local header escape did not run without API")
        self.client = build_client(CONFIG, self.args.address)
        await self.connect()
        await self.pointer(150, 30, False)
        await self.require(mode=1, menu="YES")
        await self.choose_buttons_locally()
        await self.set_mode(1, "enable-after-local-exit")
        logger.info(
            "LOCAL_ESCAPE=PASS api_disconnected=1 local_buttons=1 "
            "wifi_remained_enabled=1"
        )

    async def choose_buttons_locally(self):
        """Verify dialog dragging and use the real local Show buttons target."""
        await self.drag((450, 356), (600, 356))
        await self.require(mode=1, menu="YES")

        async def tap_buttons():
            await self.pointer(508, 356, True)
            await self.snapshot()
            await asyncio.sleep(0.15)
            await self.pointer(508, 356, False)

        async def verify_buttons():
            await asyncio.sleep(0.3)
            return await self.require(mode=0, nav="YES", menu="NO")

        await self.journal.once(
            "local:buttons", 0, tap_buttons, verify_buttons
        )

    async def reboot(self, mode, operation):
        """Record one reboot and confirm a new boot before reconnecting."""
        offset = self.args.uart_log.stat().st_size

        async def verify():
            await self.client.disconnect(force=True)
            deadline = time.monotonic() + 55
            marker = f"Project ukraine-status-panel.panel version {expected_version(CONFIG)}"
            while marker not in self.uart_since(offset):
                if time.monotonic() > deadline:
                    raise TimeoutError("Reboot boot marker not observed")
                await asyncio.sleep(0.2)
            self.client = build_client(CONFIG, self.args.address)
            await self.connect()
            state = await self.require(mode=mode)
            logger.info("MODE_PERSISTENCE=PASS mode={} new_boot=YES", mode)
            return state

        await self.journal.once(
            operation,
            {"expected_mode": mode},
            lambda: self.client.button_command(self.restart_key),
            verify,
        )

    async def cleanup(self, successful):
        """Restore the original mode once and leave uncertain writes visible."""
        try:
            if not self.client.is_connected:
                self.client = build_client(CONFIG, self.args.address)
                await self.connect()
            await self.call("usp_qa_navigation_touch_end")
            if self.journal and self.journal.needs_restore("mode"):
                await self.set_mode(self.original["mode"], "restore:mode")
                await self.call("usp_qa_show_home")
        finally:
            if self.journal:
                self.journal.finish(successful)
            await self.client.disconnect(force=True)

    async def run(self):
        """Keep restoration active for every gesture or persistence failure."""
        successful = False
        try:
            await self.connect()
            self.original = await self.snapshot()
            self.journal = ProbeJournal(self.args.journal, self.original)
            await self.set_mode(1, "change:mode")
            await self.call("usp_qa_navigation_close_menu")
            await self.check_chain()
            await self.check_rejected_movements()
            await self.check_settings_button()
            await self.check_local_escape()
            if self.args.reboots:
                await self.call("usp_qa_navigation_touch_end")
                await self.reboot(1, "reboot:fullscreen")
            await self.set_mode(0, "buttons:mode")
            if self.args.reboots:
                await self.reboot(0, "reboot:buttons")
            successful = True
            logger.info(
                "FULLSCREEN_NAVIGATION=PASS generated_pointer=1 physical_finger=0"
            )
        finally:
            await self.cleanup(successful)


async def main(args):
    """Initialize the probe only after the asynchronous loop has started."""
    await NavigationProbe(args).run()


if __name__ == "__main__":
    asyncio.run(main(arguments()))
