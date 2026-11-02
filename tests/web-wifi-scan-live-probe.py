#!/usr/bin/env python3
"""Validate repeated non-blocking authenticated Wi-Fi scans on a physical panel."""
from __future__ import annotations

import json
import os
from pathlib import Path
import time
import urllib.error
import urllib.request

from history_native_api_common import secret_value

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
BASE = f"http://{ADDRESS}"
PATH = "/usp/network/scan"
REQUIRE_VISIBLE = os.environ.get("USP_REQUIRE_VISIBLE_WIFI", "0") == "1"
REPEAT = max(1, int(os.environ.get("USP_WIFI_SCAN_REPEAT", "3")))
START_MAX_SECONDS = float(os.environ.get("USP_WIFI_SCAN_START_MAX_SECONDS", "2.0"))
WEB_MAX_SECONDS = float(os.environ.get("USP_WIFI_SCAN_WEB_MAX_SECONDS", "5.0"))


def require_digest() -> None:
    request = urllib.request.Request(BASE + PATH, headers={"Connection": "close"})
    try:
        urllib.request.urlopen(request, timeout=8)
    except urllib.error.HTTPError as exc:
        try:
            challenge = exc.headers.get("WWW-Authenticate", "")
            if exc.code == 401 and challenge.lower().startswith("digest "):
                print("WIFI_SCAN_UNAUTH=PASS status=401 auth=Digest")
                return
            raise RuntimeError(f"Unexpected unauthenticated response: {exc.code}") from exc
        finally:
            exc.close()
    raise RuntimeError("Wi-Fi scan API allowed unauthenticated access")


def opener() -> urllib.request.OpenerDirector:
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, BASE, "admin", secret_value(CONFIG, "web_server_password"))
    return urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))


def read_json(client: urllib.request.OpenerDirector, request: urllib.request.Request,
              timeout: float = 8) -> tuple[int, dict]:
    with client.open(request, timeout=timeout) as response:
        return response.status, json.loads(response.read())


def validate_networks(data: dict, raw: bytes, index: int) -> None:
    networks = data.get("networks")
    if not isinstance(networks, list):
        raise RuntimeError("Wi-Fi scan networks is not a list")
    if REQUIRE_VISIBLE and not networks:
        raise RuntimeError("Wi-Fi scan returned no visible networks")
    names: set[str] = set()
    last_rssi = 128
    for item in networks:
        if not isinstance(item, dict):
            raise RuntimeError("Wi-Fi scan entry is not an object")
        ssid, rssi = item.get("ssid"), item.get("rssi")
        channel, secure = item.get("channel"), item.get("secure")
        if not isinstance(ssid, str) or not ssid or len(ssid.encode()) > 32:
            raise RuntimeError(f"Invalid SSID: {ssid!r}")
        if ssid in names:
            raise RuntimeError(f"Duplicate SSID: {ssid!r}")
        names.add(ssid)
        if not isinstance(rssi, int) or rssi > last_rssi:
            raise RuntimeError("Wi-Fi scan is not sorted by descending RSSI")
        last_rssi = rssi
        if not isinstance(channel, int) or channel < 1 or channel > 177:
            raise RuntimeError(f"Invalid Wi-Fi channel: {channel!r}")
        if not isinstance(secure, bool):
            raise RuntimeError("Invalid Wi-Fi security flag")
    if b"password" in raw.lower() or b"psk" in raw.lower():
        raise RuntimeError("Wi-Fi scan response leaked credential fields")
    hidden = data.get("hidden", 0)
    if not isinstance(hidden, int) or hidden < 0:
        raise RuntimeError("Invalid hidden-network count")
    preview = ", ".join(f"{x['ssid']}({x['rssi']}dBm)" for x in networks[:5]) or "none"
    print(f"WIFI_SCAN_AUTH=PASS scan={index} visible={len(networks)} hidden={hidden}")
    print(f"WIFI_SCAN_LIVE=PASS scan={index} strongest={preview}")


def scan_once(client: urllib.request.OpenerDirector, index: int) -> None:
    start_request = urllib.request.Request(BASE + PATH, data=b"", method="POST")
    started = time.monotonic()
    status, data = read_json(client, start_request)
    elapsed = time.monotonic() - started
    if status != 202 or data.get("state") != "scanning":
        raise RuntimeError(f"Scan start contract failed: HTTP {status} {data}")
    if elapsed > START_MAX_SECONDS:
        raise RuntimeError(f"Scan start blocked HTTP for {elapsed:.2f}s")
    print(f"WIFI_SCAN_START=PASS scan={index} status=202 seconds={elapsed:.3f}")

    web_started = time.monotonic()
    with client.open(urllib.request.Request(BASE + "/", headers={"Connection": "close"}), timeout=WEB_MAX_SECONDS + 2) as response:
        if response.status != 200:
            raise RuntimeError(f"Web UI unavailable during scan: HTTP {response.status}")
        response.read(1)
    web_elapsed = time.monotonic() - web_started
    if web_elapsed > WEB_MAX_SECONDS:
        raise RuntimeError(f"Web UI stalled during scan for {web_elapsed:.2f}s")
    print(f"WIFI_SCAN_WEB_RESPONSIVE=PASS scan={index} seconds={web_elapsed:.3f}")

    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        request = urllib.request.Request(BASE + PATH, headers={"Cache-Control": "no-cache"})
        status, data = read_json(client, request)
        if status == 202 and data.get("state") == "scanning":
            time.sleep(0.25)
            continue
        if status != 200 or data.get("state") != "ready" or data.get("ok") is not True:
            raise RuntimeError(f"Wi-Fi scan failed: HTTP {status} {data}")
        raw = json.dumps(data, ensure_ascii=False).encode()
        validate_networks(data, raw, index)
        return
    raise RuntimeError("Wi-Fi scan did not become ready within 20 seconds")


def main() -> None:
    require_digest()
    client = opener()
    for index in range(1, REPEAT + 1):
        scan_once(client, index)


if __name__ == "__main__":
    main()
