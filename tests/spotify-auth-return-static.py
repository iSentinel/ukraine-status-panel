#!/usr/bin/env python3
"""Guard callback/inbox boundaries until the authenticated ESP adapter is added."""
import argparse
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Protect zero-I/O scope and the actual target include closure."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    header = (ROOT / "include/usp_spotify_auth_return.h").read_text()
    yaml = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "include/usp_spotify_auth_return.h" in yaml
    assert "class Exchange" in header and "TokenRequest" in header
    assert "sizeof(Exchange) <= 2048" in header
    assert "sizeof(TokenRequest) <= 1536" in header
    assert "GRANT_TTL_MS = 60U * 1000U" in header
    for forbidden in ("esp_http_client", "client_secret", "preferences"):
        assert forbidden not in header.lower(), forbidden
    relay = ROOT / "web-installer/spotify-callback"
    html = (relay / "index.html").read_text()
    assert 'name="referrer" content="no-referrer"' in html
    assert (
        "connect-src 'none'" in html and 'type="module" src="relay.js"' in html
    )
    sources = [
        (relay / "relay.js").read_text(),
        (ROOT / "firmware/webui/usp-spotify-auth-return.js").read_text(),
    ]
    for source in sources:
        for forbidden in (
            "localStorage",
            "sessionStorage",
            "fetch(",
            "XMLHttpRequest",
        ):
            assert forbidden not in source, forbidden
    assert 'history.replaceState(null, "", location.pathname)' in sources[0]
    assert "event.source !== openerWindow" in sources[0]
    assert "event.source !== this.popup" in sources[1]
    logger.info(
        "SPOTIFY_AUTH_RETURN_STATIC=PASS RAM_only no_network modules target_closure"
    )


if __name__ == "__main__":
    main()
