#!/usr/bin/env python3
"""Validate the Web Location / weather onboarding contract."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text(encoding="utf-8")


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    require(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        'id: web_group_location',
        'name: "Location"',
        'id: web_selected_region_select',
        'id: set_selected_region',
        'id: web_selected_region_name',
        'id: web_weather_city_search',
        'id(weather_client).request_city_search(x, id(ui_language))',
        'id: web_weather_location',
        'id: web_weather_search_result_1',
        'id: web_weather_search_result_2',
        'id: web_weather_search_result_3',
        'select_search_result(0)',
        'select_search_result(1)',
        'select_search_result(2)',
        'id(weather_client).request_refresh()',
    ):
        require(token in PROD, f"production missing: {token}")

    for token in (
        '31 · м. Київ / г. Киев / Kyiv City',
        '14 · Київська обл. / Киевская обл. / Kyiv Oblast',
        '9999 · АР Крим / АР Крым / Crimea',
    ):
        require(token in PROD, f"region selector missing: {token}")

    for token in (
        '"location"',
        '["Location","location"]',
        'Selected Region',
        'entity.domain === "select"',
        'Weather City Search',
        'Weather Search Result 1',
        'Use Weather Result 1',
        'Refresh Weather',
        'Результат 1',
        'Вибрати 1',
        'Выбрать 1',
    ):
        require(token in JS, f"Web UI missing: {token}")

    require(PUBLIC.exists(), "public first-flash YAML missing")
    public = PUBLIC.read_text(encoding="utf-8")
    for leaked in (
        'web_group_location',
        'web_selected_region_select',
        'web_weather_city_search',
        'web_weather_location',
        'Weather City Search',
    ):
        require(leaked not in public, f"public profile leaked Web Location token: {leaked}")

    print("WEB_LOCATION_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_LOCATION_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
