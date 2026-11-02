#!/usr/bin/env python3
"""Guard the promoted Stage4 Spotify LCD surface and public projection."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
LCD = (ROOT / "include/usp_spotify_lcd.h").read_text()
QUEUE = (ROOT / "components/usp_spotify_auth/queue_policy.h").read_text()


def section(begin: str, end: str) -> str:
    start = PRIVATE.index(begin)
    return PRIVATE[start:PRIVATE.index(end, start)]


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


seek = section("# WEB_RUNTIME_BEGIN: spotify-seek-control", "# WEB_RUNTIME_END: spotify-seek-control")
volume = section("# WEB_RUNTIME_BEGIN: spotify-volume-control", "# WEB_RUNTIME_END: spotify-volume-control")
refresh = PRIVATE[PRIVATE.index("- id: refresh_spotify_ui"):PRIVATE.index("- id: sync_lcd_navigation_page")]
require(seek.count("on_release:") == 1 and "on_value:" not in seek, "seek must commit once on release")
require(volume.count("on_release:") == 1 and "on_value:" not in volume, "volume must commit once on release")
require(PRIVATE.count("id: spotify_device_row_") == 8, "device selector must stay bounded at 8 rows")
require(PRIVATE.count("id: spotify_queue_row_") == 12, "queue view must stay bounded at 12 rows")
require(PRIVATE.count("request_devices_refresh()") == 2, "device refresh must stay explicit")
require(PRIVATE.count("request_queue_refresh()") == 2, "queue refresh must stay explicit")
require(PRIVATE.count("request_queue_add(uri)") == 1, "queue add must have one UI mutation path")
require(PRIVATE.count("request_shuffle_toggle()") == 1, "shuffle must have one explicit mutation path")
require(PRIVATE.count("request_repeat(usp_spotify_modes::Repeat::") == 3, "repeat must expose exactly three explicit states")
for token in ("request_seek(", "request_volume(", "request_transfer(", "request_queue_refresh", "request_queue_add", "request_shuffle_toggle", "request_repeat("):
    require(token not in refresh, f"periodic presentation must not mutate provider state: {token}")
for token in ("spotify_auth_api", "spotify_device_modal", "spotify_modes_modal", "spotify_queue_modal"):
    require(token not in PUBLIC, f"public profile leaked private Stage4 token: {token}")
require("LV_STATE_PRESSED" in LCD, "GET refresh must not move a pressed value control")
require("copy_utf8(next.name,name)" in QUEUE and "copy_ascii(next.uri,uri)" in QUEUE, "queue must separate UTF-8 display text from strict URI")
require("# WEB_RUNTIME_BEGIN: spotify-stage4-page-buttons" in PRIVATE, "private Stage4 page buttons must be stripped from public profile")
require("SPOTIFY_STAGE4_NAV_CLEANUP" in (ROOT / "scripts/generate-public-first-flash.py").read_text(), "public generator must audit-remove private modal cleanup")
print("SPOTIFY_STAGE4_LCD_STATIC=PASS value device modes queue public_projection")
