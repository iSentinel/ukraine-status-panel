#!/usr/bin/env python3
"""Validate the optional SHT4x indoor Weather UI contract."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD_PATH = ROOT / "firmware/ukraine-status-panel.yaml"
PUBLIC_PATH = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"


def require(ok: bool, message: str) -> None:
    """Fail with a concise contract message."""
    if not ok:
        raise AssertionError(message)


def validate_catalog(text: str, label: str) -> None:
    """Require every real branch to use the shared unchanged text catalog."""
    source = (ROOT / "include/usp_indoor_text.h").read_text()
    start = text.index("  - id: refresh_indoor_environment_ui\n")
    block = text[start:].split("\n  - id:", 1)[0]
    for key in ("PREFIX", "NOT_CONNECTED", "WAITING"):
        require(
            block.count("usp_indoor_text::Key::" + key) == 3,
            f"{label}: incomplete {key} catalog binding",
        )
    require("include/usp_indoor_text.h" in text, f"{label}: catalog include")
    for value in (
        "В приміщенні: ",
        "В помещении: ",
        "Indoor: ",
        "датчик не підключено",
        "датчик не подключен",
        "sensor not connected",
        "очікування даних",
        "ожидание данных",
        "waiting for data",
    ):
        require('"' + value + '"' in source, f"catalog missing {value}")


def validate_profile(text: str, label: str) -> None:
    """Check that one generated profile retains the indoor Weather feature."""
    validate_catalog(text, label)
    for token in (
        CURRENT_FIRMWARE_MARKER,
        "id: weather_indoor_label",
        "id: fullscreen_weather_indoor_label",
        "id: refresh_indoor_environment_ui",
        "id(environment_sht4x).detected()",
        "id(environment_sht4x).last_read_ok()",
        "id(environment_temperature).state",
        "id(environment_humidity).state",
        "0x94A3B8",
        "0xFACC15",
        "0x4ADE80",
    ):
        require(token in text, f"{label}: missing token: {token}")

    refresh_call = "- script.execute: refresh_indoor_environment_ui"
    require(
        text.count(refresh_call) >= 4,
        f"{label}: Weather/SHT refresh wiring is incomplete",
    )
    for obsolete in (
        "weather_city_note_label",
        "City stored locally",
        "Город сохранен локально",
        "Місто збережено локально",
    ):
        require(
            obsolete not in text, f"{label}: obsolete city note: {obsolete}"
        )


def main() -> int:
    """Validate production and public first-flash profiles."""
    validate_profile(PROD_PATH.read_text(encoding="utf-8"), "production")
    require(PUBLIC_PATH.exists(), "public first-flash YAML is missing")
    validate_profile(PUBLIC_PATH.read_text(encoding="utf-8"), "public")
    print("WEATHER_INDOOR_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEATHER_INDOOR_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
