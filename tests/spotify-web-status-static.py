#!/usr/bin/env python3
"""Stage5 main Web UI contract: dedicated local-only Spotify configuration tab."""
import argparse
from loguru import logger
from pathlib import Path


REQUIRED = (
    '"spotify", "maintenance"',
    'spotify:"Spotify"',
    'this.__uspPrivate_activeTab === "spotify"',
    '"/usp/spotify/auth/status"',
    'credentials:"same-origin"',
    'cache:"no-store"',
    "session_storage",
    "__uspLoadSpotifyStatus",
    "dataset.spotifyState",
    "status.error?t.spotifyError:",
    'location.href="/usp/spotify/auth/"',
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--source",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "firmware/webui/usp-web-ui.src.js",
    )
    args = ap.parse_args()
    s = args.source.read_text()
    for token in REQUIRED:
        assert token in s, token
    overview = s.split("render() {", 1)[1].split("__uspRenderHeader", 1)[0]
    assert (
        'activeTab === "overview") content.append(this.spotifyPanel'
        not in overview
    )
    loader = s[
        s.index("async __uspLoadSpotifyStatus") : s.index(
            "spotifyPanel(t)", s.index("async __uspLoadSpotifyStatus")
        )
    ]
    assert (
        "accounts.spotify.com" not in loader
        and "api.spotify.com" not in loader
    )
    assert (
        "access_token" not in loader
        and "refresh_token" not in loader
        and "client_id" not in loader
    )
    assert "localStorage" not in loader and "sessionStorage" not in loader
    assert 'method:"POST"' not in loader
    logger.info("Spotify Web status contract passed without device contact")
    print(
        "SPOTIFY_WEB_STATUS_STATIC=PASS dedicated_tab local_status_only no_tokens no_provider no_post"
    )


if __name__ == "__main__":
    main()
