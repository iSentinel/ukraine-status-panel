#!/usr/bin/env python3
"""Guard the read-only Spotify snapshot-to-LVGL binding."""
from pathlib import Path
import runpy
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_public_projection():
    """Prove public generation excludes private symbols and rejects a lost marker."""
    public = (
        ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
    ).read_text()
    for token in (
        "id(spotify_auth_api)",
        "usp_spotify_presentation::",
        "usp_spotify_playback_worker::",
        "Local-only Spotify interpolation",
    ):
        assert token not in public, token
    assert "Spotify is not connected" in public
    generator = runpy.run_path(
        str(ROOT / "scripts/generate-public-first-flash.py")
    )
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    source = source.replace(
        "# WEB_RUNTIME_BEGIN: spotify-lcd-presentation",
        "# REMOVED: spotify-lcd-presentation",
        1,
    )
    with tempfile.TemporaryDirectory(prefix="usp-public-spotify-") as folder:
        path = Path(folder) / "source.yaml"
        path.write_text(source)
        try:
            generator["generate"](path)
        except RuntimeError:
            return
    raise AssertionError("Public generation accepted a missing private marker")


def check_main_binding():
    """Require a short main-only binding and disabled controls in its helper."""
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    header = (
        ROOT / "components/usp_spotify_auth/playback_presentation.h"
    ).read_text()
    helper = (ROOT / "include/usp_spotify_lcd.h").read_text()
    auth = (ROOT / "components/usp_spotify_auth/spotify_auth.h").read_text()
    assert "PlaybackPhase *phase" in auth and "copy_playback" in auth
    start = source.index("  - id: refresh_spotify_ui")
    end = source.index("\n  - id:", start + 8)
    body = source[start:end]
    for token in (
        "copy_playback(",
        "usp_spotify_presentation::project(",
        "usp_spotify_lcd::render(",
    ):
        assert token in body, token
    assert len(body.splitlines()) <= 24
    for token in ("lv_slider_set_value", "LV_ANIM_OFF", "LV_STATE_DISABLED"):
        assert token in helper
    assert "lv_obj_clear_state" not in helper
    assert "image_url" not in body and "http" not in body.lower()
    assert "Mode::PLAYING" in header and "Mode::REAUTH" in header
    assert "id(lcd_navigation_page) == 12" in source
    for endpoint in ("/me/player/play", "/me/player/pause", "/me/player/next"):
        assert endpoint not in source


def main():
    """Check LCD binding and private/public profile separation."""
    check_public_projection()
    check_main_binding()
    logger.info(
        "SPOTIFY_PLAYBACK_PRESENTATION_STATIC=PASS copy_only main_lvgl "
        "change_only controls_disabled artwork0 network0"
    )


if __name__ == "__main__":
    main()
