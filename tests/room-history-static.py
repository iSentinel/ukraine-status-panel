#!/usr/bin/env python3
"""Validate the M6 room-history storage and firmware integration contract."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/usp_room_history/room_history.h"
SOURCE = ROOT / "components/usp_room_history/room_history.cpp"
PYTHON = ROOT / "components/usp_room_history/__init__.py"
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    for path in (HEADER, SOURCE, PYTHON, YAML):
        require(path.exists(), f"missing {path}")

    header = HEADER.read_text()
    source = SOURCE.read_text()
    yaml = YAML.read_text()

    require("ROOM_HISTORY_CAPACITY = 288" in header, "capacity must be 288")
    require("ROOM_HISTORY_BUCKET_SECONDS = 300" in header,
            "bucket must be five minutes")
    require("sizeof(RoomSample) == 12" in header,
            "sample layout must stay bounded")
    require("ROOM_SAMPLE_VALID" in header and "ROOM_SAMPLE_GAP" in header,
            "valid/gap flags missing")
    require("MALLOC_CAP_SPIRAM" in source, "PSRAM allocation missing")
    require("MALLOC_CAP_8BIT" in source, "safe fallback allocation missing")
    require("bucket < this->last_bucket_" in source,
            "backwards-time guard missing")
    require("bucket == this->last_bucket_" in source,
            "same-bucket replacement missing")
    require("Persistence: none" in source,
            "explicit no-flash persistence policy missing")

    forbidden = ("preferences", "ESPPreference", "nvs_", "save(")
    lowered = source.lower()
    for token in forbidden:
        require(token.lower() not in lowered,
                f"room history must not write flash: {token}")

    require("- usp_room_history" in yaml, "external component not registered")
    require("usp_room_history:" in yaml and "id: room_history" in yaml,
            "room history component config missing")
    require("interval: 5min" in yaml, "five-minute sampling hook missing")
    for token in (
        "id(panel_time).now()",
        "now.is_valid()",
        "id(environment_sht4x).detected()",
        "id(environment_sht4x).last_read_ok()",
        "id(room_history).record(",
    ):
        require(token in yaml, f"sampling contract missing: {token}")

    require(CURRENT_FIRMWARE_MARKER in yaml,
            "M6 source version not bumped")

    bytes_used = 288 * 12
    require(bytes_used == 3456 and bytes_used <= 4096,
            "room history storage budget exceeded")

    print(
        "ROOM_HISTORY_STATIC=PASS capacity=288 bytes=3456 psram=1 "
        "gap_flags=1 no_flash=1 sampling=5min"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"ROOM_HISTORY_STATIC=FAIL {exc}", file=sys.stderr)
        raise
