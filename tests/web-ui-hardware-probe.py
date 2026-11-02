#!/usr/bin/env python3
"""Validate authenticated Web UI controls against the shared settings backend."""

from __future__ import annotations

import asyncio
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request

from aioesphomeapi import LogLevel

from history_native_api_common import build_client, secret_value, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
BASE_URL = f"http://{ADDRESS}"
USERNAME = "admin"

SNAPSHOT_RE = re.compile(
    r"SETTINGS134 language=(\d+) palette=(\d+) ui_region=(\d+) "
    r"stored_region=(\d+) outage=(\d+) backlight=([0-9.]+) "
    r"light=([0-9.]+) slider=(-?\d+)"
)

LANG_OPTIONS = ("Українська", "Русский", "English")
PALETTE_OPTIONS = ("Standard", "CVD-friendly", "Monochrome")


def build_web_opener(password: str) -> urllib.request.OpenerDirector:
    """Create one Digest-authenticated local HTTP opener."""
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, BASE_URL, USERNAME, password)
    return urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))


def require_digest_challenge() -> None:
    """Confirm unauthenticated access is rejected with a Digest challenge."""
    try:
        urllib.request.urlopen(urllib.request.Request(f"{BASE_URL}/", headers={"Connection": "close"}), timeout=8)
    except urllib.error.HTTPError as exc:
        challenge = exc.headers.get("WWW-Authenticate", "")
        if exc.code != 401 or not challenge.lower().startswith("digest "):
            raise RuntimeError(f"Unexpected unauthenticated response: {exc.code}")
        print("WEB_UNAUTH=PASS status=401 auth=Digest")
        return
    raise RuntimeError("Web root allowed unauthenticated access")


def require_local_frontend(opener: urllib.request.OpenerDirector) -> None:
    """Confirm authenticated UI loads and contains no external HTTP(S) URLs."""
    with opener.open(urllib.request.Request(f"{BASE_URL}/", headers={"Connection": "close"}), timeout=10) as response:
        body = response.read().decode("utf-8", errors="replace")
        if response.status != 200:
            raise RuntimeError(f"Authenticated web root returned {response.status}")
    external = re.findall(r"https?://[^\"'<>\s]+", body, re.IGNORECASE)
    if external:
        raise RuntimeError(f"External Web UI URLs found: {external[:3]}")
    print(f"WEB_AUTH=PASS status=200 bytes={len(body.encode('utf-8'))}")
    print("WEB_LOCAL_FRONTEND=PASS external_urls=0")


def web_set(
    opener: urllib.request.OpenerDirector,
    domain: str,
    entity_name: str,
    params: dict[str, str],
) -> None:
    """POST one ESPHome entity update through the authenticated Web API."""
    encoded_name = urllib.parse.quote(entity_name, safe="")
    path = f"/{domain}/{encoded_name}/set"
    query = urllib.parse.urlencode(params)
    request = urllib.request.Request(
        f"{BASE_URL}{path}?{query}",
        data=b"",
        method="POST",
        headers={"Connection": "close"},
    )
    with opener.open(request, timeout=10) as response:
        if response.status not in (200, 204):
            raise RuntimeError(f"Web set failed: {path} -> {response.status}")


def text_of(message) -> str:
    """Decode an ESPHome log message."""
    value = getattr(message, "message", b"")
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else str(value)


async def dump_snapshot(client, service, lines: list[str]) -> tuple[int, ... | float]:
    """Request one settings snapshot and parse it from Native API logs."""
    start = len(lines)
    await client.execute_service(service, {})
    for _ in range(50):
        for line in lines[start:]:
            match = SNAPSHOT_RE.search(line)
            if match:
                raw = match.groups()
                return (
                    *(int(value) for value in raw[:5]),
                    *(float(value) for value in raw[5:7]),
                    int(raw[7]),
                )
        await asyncio.sleep(0.1)
    raise RuntimeError("Shared-settings snapshot timeout")


async def wait_snapshot_value(client, service, lines, index: int, expected: int, timeout: float = 8.0):
    """Poll shared state until one integer field reaches its expected value."""
    loops = max(1, int(timeout / 0.5))
    last = None
    for _ in range(loops):
        last = await dump_snapshot(client, service, lines)
        if int(last[index]) == int(expected):
            return last
        await asyncio.sleep(0.5)
    raise RuntimeError(f"Shared setting index {index} did not reach {expected}; last={last}")


async def main() -> None:
    """Run Digest, entity-surface, HTTP mutation, and restoration checks."""
    require_digest_challenge()
    password = secret_value(CONFIG, "web_server_password")
    opener = build_web_opener(password)
    require_local_frontend(opener)

    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    lines: list[str] = []
    long_ops: list[str] = []

    def on_log(message) -> None:
        text = text_of(message)
        lines.append(text)
        if "lvgl took a long time" in text:
            long_ops.append(text)

    stop = client.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_DEBUG, dump_config=False)
    original = None
    entities_by_name = {}
    try:
        await verify_device(client, CONFIG)
        entities, services = await client.list_entities_services()
        entities_by_name = {str(getattr(entity, "name", "")): entity for entity in entities}
        by_service = {str(service.name): service for service in services}
        if "usp_qa_dump_shared_settings_134" not in by_service:
            raise RuntimeError("Missing shared-settings snapshot service")
        snapshot_service = by_service["usp_qa_dump_shared_settings_134"]

        controls = {str(getattr(entity, "name", "")) for entity in entities}
        required = {"UI Language", "Alert Palette", "Outage Group", "Panel Backlight", "Restart"}
        missing = required - controls
        qa_public = sorted(name for name in controls if name.startswith("QA Render"))
        if missing or qa_public:
            raise RuntimeError(f"Unsafe Web entity surface: missing={sorted(missing)} qa={qa_public}")
        print("WEB_ENTITY_SURFACE=PASS required=5 qa_render_public=0")

        original = await dump_snapshot(client, snapshot_service, lines)
        language, palette, _ui_region, _stored_region, outage = original[:5]
        target_language = (int(language) + 1) % len(LANG_OPTIONS)
        target_palette = (int(palette) + 1) % len(PALETTE_OPTIONS)
        target_outage = int(outage) + 1 if int(outage) < 60 else int(outage) - 1

        web_set(opener, "select", "UI Language", {"option": LANG_OPTIONS[target_language]})
        await wait_snapshot_value(client, snapshot_service, lines, 0, target_language)
        print(f"WEB_LANGUAGE_SET=PASS target={target_language}")
        await asyncio.sleep(2.0)

        web_set(opener, "select", "Alert Palette", {"option": PALETTE_OPTIONS[target_palette]})
        await wait_snapshot_value(client, snapshot_service, lines, 1, target_palette)
        print(f"WEB_PALETTE_SET=PASS target={target_palette}")
        await asyncio.sleep(3.0)

        web_set(opener, "number", "Outage Group", {"value": str(target_outage)})
        await wait_snapshot_value(client, snapshot_service, lines, 4, target_outage)
        print(f"WEB_OUTAGE_SET=PASS target={target_outage}")
        await asyncio.sleep(1.0)

        web_set(opener, "select", "UI Language", {"option": LANG_OPTIONS[int(language)]})
        await wait_snapshot_value(client, snapshot_service, lines, 0, int(language))
        web_set(opener, "select", "Alert Palette", {"option": PALETTE_OPTIONS[int(palette)]})
        await wait_snapshot_value(client, snapshot_service, lines, 1, int(palette))
        web_set(opener, "number", "Outage Group", {"value": str(int(outage))})
        restored = await wait_snapshot_value(client, snapshot_service, lines, 4, int(outage))
        if tuple(int(v) for v in restored[:5]) != tuple(int(v) for v in original[:5]):
            raise RuntimeError(f"Shared settings not restored: original={original[:5]} final={restored[:5]}")
        print("WEB_SETTINGS_RESTORE=PASS")
        await asyncio.sleep(2.0)

        if long_ops:
            raise RuntimeError(f"LVGL long operations during Web control test: {len(long_ops)}")
        print("WEB_LVGL_LONG_OP=0")
        print("WEB_UI_HARDWARE=PASS")
    finally:
        # Restoration must not depend on the Web endpoint that is under test.
        # If any HTTP mutation stalls/fails, restore through Native API before
        # disconnecting so hardware validation never leaves user settings changed.
        if original is not None and entities_by_name:
            try:
                language, palette, _ui_region, _stored_region, outage = original[:5]
                client.select_command(
                    entities_by_name["UI Language"].key,
                    LANG_OPTIONS[int(language)],
                )
                client.select_command(
                    entities_by_name["Alert Palette"].key,
                    PALETTE_OPTIONS[int(palette)],
                )
                client.number_command(
                    entities_by_name["Outage Group"].key,
                    float(outage),
                )
                await asyncio.sleep(1.0)
                print("WEB_SETTINGS_FAILSAFE_RESTORE=PASS")
            except Exception as exc:
                print(f"WEB_SETTINGS_FAILSAFE_RESTORE=WARN {type(exc).__name__}: {exc}")
        stop()
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
