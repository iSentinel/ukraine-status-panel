#!/usr/bin/env python3
"""Verify that the physical panel serves the checked-in custom Web UI."""

from __future__ import annotations

import gzip
import os
from pathlib import Path
import urllib.request

from history_native_api_common import secret_value

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(
    os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml")
)
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
BASE_URL = f"http://{ADDRESS}"
USERNAME = "admin"


def build_opener(password: str) -> urllib.request.OpenerDirector:
    """Build a Digest-authenticated opener without logging credentials."""
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, BASE_URL, USERNAME, password)
    handler = urllib.request.HTTPDigestAuthHandler(manager)
    return urllib.request.build_opener(handler)


def fetch(opener: urllib.request.OpenerDirector, path: str) -> bytes:
    """Fetch one local Web asset and transparently decode gzip content."""
    request = urllib.request.Request(
        f"{BASE_URL}{path}", headers={"Connection": "close"}
    )
    with opener.open(request, timeout=10) as response:
        if response.status != 200:
            raise RuntimeError(f"{path} returned HTTP {response.status}")
        body = response.read()
        if response.headers.get("Content-Encoding", "").lower() == "gzip":
            body = gzip.decompress(body)
        return body


def require_equal(path: str, actual: bytes, expected: Path) -> None:
    """Require a live asset to match its checked-in source byte-for-byte."""
    wanted = expected.read_bytes()
    if actual != wanted:
        raise RuntimeError(
            f"{path} mismatch: live={len(actual)} source={len(wanted)}"
        )
    print(f"WEB_CUSTOM_ASSET=PASS path={path} bytes={len(actual)}")


def main() -> None:
    """Validate root shell and both embedded custom Web assets."""
    password = secret_value(CONFIG, "web_server_password")
    opener = build_opener(password)
    root = fetch(opener, "/").decode("utf-8", errors="strict")
    for marker in ("/0.css", "/0.js", "<esp-app></esp-app>"):
        if marker not in root:
            raise RuntimeError(f"Web root missing marker: {marker}")
    print(f"WEB_CUSTOM_ROOT=PASS bytes={len(root.encode())}")

    require_equal(
        "/0.css", fetch(opener, "/0.css"), ROOT / "firmware/webui/usp-web-ui.css"
    )
    require_equal(
        "/0.js", fetch(opener, "/0.js"), ROOT / "firmware/webui/usp-web-ui.js"
    )
    print("WEB_CUSTOM_LIVE=PASS")


if __name__ == "__main__":
    main()
