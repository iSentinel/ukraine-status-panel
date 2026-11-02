#!/usr/bin/env python3
"""Generate the public USB provisioning profile without network control or secrets."""
from __future__ import annotations
import argparse
from pathlib import Path
import re
from loguru import logger

IDENTITY_OLD = """esphome:
  name: "${device_name}"
  friendly_name: "${friendly_name}"
  comment: "Waveshare ESP32-S3-Touch-LCD-7B hardware bring-up"
"""
IDENTITY_NEW = """esphome:
  name: "${device_name}"
  friendly_name: "${friendly_name}"
  name_add_mac_suffix: true
  build_path: build/ukraine-status-panel-public-first-flash
  comment: "Ukraine Status Panel public first-flash image"
"""
NETWORK_OLD = """ota:
  - platform: esphome
    encryption: {}

# First bring-up deliberately does not depend on your private Wi-Fi secrets.
# It will expose an ESPHome fallback AP after flashing.
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  # Restore only the persisted IP layer after association. Never invoke the
  # credential transaction from boot/on_connect; that would reintroduce the
  # initial Wi-Fi scan/reconnect race fixed in 0.115.182.
  on_connect:
    - lambda: id(home_native_frame_data_generation)++;
    - script.execute: refresh_wifi_icon
    - script.execute: restore_static_network_profile
  on_disconnect:
    - lambda: |-
        id(home_native_frame_data_generation)++;
        id(panel_wifi_rssi_valid) = false;
    - script.execute: refresh_wifi_icon

  ap:
    ssid: "${friendly_name} Fallback"
    password: !secret fallback_ap_password
    ap_timeout: 90s
captive_portal:
"""
NETWORK_NEW = """# No network firmware upload in the public USB provisioning profile.

# Public first-flash image: no private Wi-Fi credentials are embedded.
wifi:
  on_connect:
    - script.execute: refresh_wifi_icon
  on_disconnect:
    - lambda: id(panel_wifi_rssi_valid) = false;
    - script.execute: refresh_wifi_icon
  ap:
    ssid: "${friendly_name} Setup"
    ap_timeout: 15s

captive_portal:

improv_serial:

# Close bootstrap provisioning transports after a bounded setup window.
provisioning:
  timeout: 15min
"""
SPOTIFY_STAGE4_NAV_CLEANUP = """\
          if (page != 12 && !lv_obj_has_flag(id(spotify_device_modal), LV_OBJ_FLAG_HIDDEN)) {
            usp_spotify_device_modal::state().close();
            usp_lcd_navigation::hidden(id(spotify_device_modal), true);
            usp_lcd_navigation::controller().set_modal_root(nullptr);
          }
          if (page != 12 && !lv_obj_has_flag(id(spotify_modes_modal), LV_OBJ_FLAG_HIDDEN)) {
            usp_lcd_navigation::hidden(id(spotify_modes_modal), true);
            usp_lcd_navigation::controller().set_modal_root(nullptr);
          }
          if (page != 12 && !lv_obj_has_flag(id(spotify_queue_modal), LV_OBJ_FLAG_HIDDEN)) {
            usp_lcd_navigation::hidden(id(spotify_queue_modal), true);
            usp_lcd_navigation::controller().set_modal_root(nullptr);
          }
"""

SPOTIFY_PUBLIC_REFRESH = """\
      - lambda: |-
          if (!id(lcd_navigation_ready)) return;
          const unsigned lang = id(ui_language) < 3 ? id(ui_language) : 0;
          const char *status[] = {"НЕ ПІДКЛЮЧЕНО", "НЕ ПОДКЛЮЧЕНО", "NOT CONNECTED"};
          const char *track[] = {"Spotify не підключено", "Spotify не подключён", "Spotify is not connected"};
          const char *artist[] = {"Підключення через Web UI · OAuth PKCE", "Подключение через Web UI · OAuth PKCE", "Connect in Web UI · OAuth PKCE"};
          const char *album[] = {"Альбом: —", "Альбом: —", "Album: —"};
          const char *art[] = {"ОБКЛАДИНКА", "ОБЛОЖКА", "ALBUM ART"};
          const char *no_art[] = {"Немає обкладинки", "Нет обложки", "No artwork"};
          const char *art_hint[] = {"З'явиться після авторизації", "Появится после авторизации", "Artwork appears after authorization"};
          const char *volume[] = {"Гучність", "Громкость", "Volume"};
          const char *device[] = {"Пристрій", "Устройство", "Device"};
          const char *hint[] = {"Керування стане доступним після PKCE-авторизації.", "Управление станет доступно после PKCE-авторизации.", "Controls become available after PKCE authorization."};
          const char *prev[] = {"Назад", "Назад", "Prev"};
          const char *play[] = {"Play", "Play", "Play"};
          const char *next[] = {"Далі", "Далее", "Next"};
          usp_lcd_navigation::label_text(id(spotify_status), status[lang]);
          usp_lcd_navigation::label_text(id(spotify_track), track[lang]);
          usp_lcd_navigation::label_text(id(spotify_artist), artist[lang]);
          usp_lcd_navigation::label_text(id(spotify_album), album[lang]);
          usp_lcd_navigation::label_text(id(spotify_art_placeholder_label), art[lang]);
          usp_lcd_navigation::label_text(id(spotify_art_caption), no_art[lang]);
          usp_lcd_navigation::label_text(id(spotify_art_hint), art_hint[lang]);
          usp_lcd_navigation::label_text(id(spotify_volume_caption), volume[lang]);
          usp_lcd_navigation::label_text(id(spotify_device_caption), device[lang]);
          usp_lcd_navigation::label_text(id(spotify_connection_hint), hint[lang]);
          usp_lcd_navigation::button_text(id(spotify_prev), prev[lang]);
          usp_lcd_navigation::button_text(id(spotify_play), play[lang]);
          usp_lcd_navigation::button_text(id(spotify_next), next[lang]);
          usp_lcd_navigation::button_text(id(spotify_device), device[lang]);
          for (auto *control : {id(spotify_prev), id(spotify_play), id(spotify_next), id(spotify_device), id(spotify_progress), id(spotify_volume)})
            lv_obj_add_state(control, LV_STATE_DISABLED);
"""

FORBIDDEN_WEB = (
    "web_server_password",
    "web_password_override",
    "web_debug_mode",
    "web_password_reset_ui_button",
    "web_security_status",
    "web_group_status",
    "web_firmware_version",
    "web_wifi_rssi",
    "New Web Password",
    "usp_svitlobot",
    "usp_starlink",
    "web_svitlobot",
    "web_starlink",
    "web_shelly",
    "web_network_",
    "web_group_location",
    "web_selected_region_select",
    "web_weather_",
    "Weather City Search",
    "SvitloBot Channel Key",
    "Starlink Host",
    "usp_ota_guard",
    "ota_guard",
    "web_ota_device_verifier",
    "usp_wifi_scan",
    "id(spotify_auth_api)",
    "usp_spotify_lcd.h",
    "usp_spotify_lcd::",
    "usp_spotify_presentation::",
    "usp_spotify_playback_worker::",
    "usp_spotify_auth:\n",
    "      - usp_spotify_auth\n",
    "/usp/spotify/auth/",
    "/usp/network/scan",
    "restore_static_network_profile",
)


def strip_marked_blocks(text: str, marker: str) -> str:
    """Remove explicitly marked production-only blocks, including nested YAML."""
    pattern = re.compile(
        rf"(?ms)^[ \t]*# {re.escape(marker)}_BEGIN(?::[^\n]*)?\n.*?"
        rf"^[ \t]*# {re.escape(marker)}_END(?::[^\n]*)?\n?"
    )
    cleaned, count = pattern.subn("", text)
    if count == 0:
        raise RuntimeError(f"no {marker} marker blocks found")
    return cleaned


def replace_once(text: str, old: str, new: str, label: str) -> str:
    """Replace one audited production block or fail loudly."""
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected 1 production block, found {count}"
        )
    return text.replace(old, new, 1)


def remove_native_api(text: str) -> str:
    """Remove the entire plaintext bootstrap API, including diagnostic actions."""
    cleaned, count = re.subn(r"(?ms)^api:\n.*?(?=^safe_mode:)", "", text)
    if count != 1:
        raise RuntimeError("expected one Native API block before safe_mode")
    return cleaned


def verify_public_profile(text: str) -> None:
    """Fail closed if bootstrap still exposes network control or private data."""
    leaked = [token for token in FORBIDDEN_WEB if token in text]
    if leaked:
        raise RuntimeError(
            f"public profile contains Web runtime tokens: {leaked}"
        )
    if "      - captive_portal\n" not in text:
        raise RuntimeError(
            "public profile lacks Wi-Fi-only captive portal override"
        )
    if "!secret" in text:
        raise RuntimeError("public profile contains secret references")
    if re.search(r"(?m)^(?:api|ota):", text):
        raise RuntimeError("public profile exposes Native API or OTA")
    if re.search(r"(?m)^    - action:", text):
        raise RuntimeError("public profile exposes custom Native API actions")


def replace_spotify_presentation(text: str) -> str:
    """Keep a disconnected public screen without a private worker dependency."""
    pattern = re.compile(
        r"(?ms)^      # WEB_RUNTIME_BEGIN: spotify-lcd-presentation\n.*?"
        r"^      # WEB_RUNTIME_END: spotify-lcd-presentation\n"
    )
    result, count = pattern.subn(lambda match: SPOTIFY_PUBLIC_REFRESH, text)
    if count != 1:
        raise RuntimeError("expected one private Spotify presentation block")
    return result


def generate(source: Path) -> str:
    """Keep the display and provisioning while closing firmware/API mutation."""
    text = replace_once(
        source.read_text(), IDENTITY_OLD, IDENTITY_NEW, "identity"
    )
    text = remove_native_api(text)
    text = replace_once(
        text, NETWORK_OLD, NETWORK_NEW, "OTA/Wi-Fi provisioning"
    )
    text = replace_spotify_presentation(text)
    text = replace_once(
        text, SPOTIFY_STAGE4_NAV_CLEANUP, "", "Spotify Stage4 navigation cleanup"
    )
    text = strip_marked_blocks(text, "WEB_RUNTIME")
    text = strip_marked_blocks(text, "PRIVATE_UPDATE")
    text = text.replace("  initial_level: INFO\n", "", 1)
    verify_public_profile(text)
    return text


def main() -> None:
    """Expose the same source/output command used by CI and packaging."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    text = generate(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    logger.info(
        "Public profile generated; Native API removed; Wi-Fi-only portal required; USB hardware acceptance pending: {}",
        args.output,
    )


if __name__ == "__main__":
    main()
