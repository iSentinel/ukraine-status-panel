#!/usr/bin/env python3
"""Guard the M7B Spotify UI shell before network/auth integration."""
import importlib.util
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML_PATH = ROOT / "firmware/ukraine-status-panel.yaml"


def load_config():
    path = ROOT / "tests/lcd-navigation-static.py"
    spec = importlib.util.spec_from_file_location("lcd_nav_static", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load_config()


def check_refresh(config):
    """Check the short YAML binding and actual localized helper strings."""
    refresh = next(
        item for item in config["script"] if item["id"] == "refresh_spotify_ui"
    )
    body = refresh["then"][0]["lambda"]
    captions = (
        ROOT / "components/usp_spotify_auth/playback_status.h"
    ).read_text()
    for token in (
        "НЕ ПІДКЛЮЧЕНО",
        "НЕ ПОДКЛЮЧЕНО",
        "NOT CONNECTED",
        "OAuth PKCE",
    ):
        assert token in captions
    assert "usp_spotify_lcd::render" in body
    assert (
        "LV_STATE_DISABLED" in (ROOT / "include/usp_spotify_lcd.h").read_text()
    )


def main():
    """Preserve the Spotify navigation and read-only screen contract."""
    config = load_config()
    source = YAML_PATH.read_text()
    pages = {page["id"]: page for page in config["lvgl"][0]["pages"]}
    page = pages["page_spotify"]
    assert page["scrollable"] is False
    assert {
        "script.execute": {"id": "sync_lcd_navigation_page", "page": 12}
    } in page["on_load"]
    assert {"script.execute": "refresh_spotify_ui"} in page["on_load"]
    for widget in page["widgets"]:
        _, item = next(iter(widget.items()))
        if (
            "y" in item
            and "height" in item
            and item.get("id") in {"spotify_art_card", "spotify_player_card"}
        ):
            assert item["y"] + item["height"] <= 524, item["id"]
    for control in (
        "spotify_prev",
        "spotify_play",
        "spotify_next",
        "spotify_device",
    ):
        assert f"id: {control}" in source
    check_refresh(config)
    assert "client_secret" not in source.lower()
    assert "SPOTIFY_CLIENT_SECRET" not in source
    swipe = (ROOT / "include/usp_swipe_navigation.h").read_text()
    assert "MAIN_PAGES[] = {0, 1, 2, 3, 4, 12, 9}" in swipe
    assert "page == 12" in source and "id: page_spotify" in source
    logger.info(
        "SPOTIFY_PAGE_STATIC=PASS page12 ui_shell=1 pkce_hint=1 secret=0 nav=8"
    )


if __name__ == "__main__":
    main()
