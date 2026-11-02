#!/usr/bin/env python3
"""Hold one authenticated SSE session across the OTA rollback window."""
from __future__ import annotations

import asyncio
import os
from pathlib import Path
import time
import urllib.request

from history_native_api_common import build_client, expected_version, secret_value

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
DURATION = float(os.environ.get("USP_SSE_SOAK_SECONDS", "75"))
EXPECTED = os.environ.get("USP_EXPECTED_VERSION", "")
USERNAME = "admin"


async def api_version() -> str:
    """Return the project version advertised by the physical panel."""
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    try:
        info = await client.device_info()
        return str(getattr(info, "project_version", "") or "")
    finally:
        await client.disconnect()


def build_opener() -> urllib.request.OpenerDirector:
    """Create a Digest-authenticated local HTTP opener."""
    base = f"http://{ADDRESS}"
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, base, USERNAME, secret_value(CONFIG, "web_server_password"))
    return urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))


def sse_soak() -> tuple[float, int, int]:
    """Read authenticated SSE until the requested soak interval is satisfied."""
    request = urllib.request.Request(
        f"http://{ADDRESS}/events",
        headers={"Accept": "text/event-stream", "Cache-Control": "no-cache"},
    )
    start = time.monotonic()
    lines = 0
    bytes_seen = 0
    with build_opener().open(request, timeout=20) as response:
        content_type = response.headers.get("Content-Type", "")
        if response.status != 200 or "text/event-stream" not in content_type:
            raise RuntimeError(f"Unexpected SSE response: {response.status} {content_type!r}")
        while time.monotonic() - start < DURATION:
            raw = response.readline()
            if not raw:
                raise RuntimeError("SSE stream closed before soak completed")
            lines += 1
            bytes_seen += len(raw)
    return time.monotonic() - start, lines, bytes_seen


def main() -> None:
    """Verify identity before/after a real SSE session and reject rollback/crash."""
    wanted = EXPECTED or expected_version(CONFIG)
    before = asyncio.run(api_version())
    if before != wanted:
        raise RuntimeError(f"Unexpected pre-soak version: {before!r}, expected {wanted!r}")
    elapsed, lines, bytes_seen = sse_soak()
    after = asyncio.run(api_version())
    if after != wanted:
        raise RuntimeError(f"Version changed after SSE soak: {before!r} -> {after!r}")
    print(
        "WEB_SSE_MEMORY_LIVE=PASS "
        f"version={after} seconds={elapsed:.1f} lines={lines} bytes={bytes_seen}"
    )


if __name__ == "__main__":
    main()
