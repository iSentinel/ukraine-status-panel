#!/usr/bin/env python3
"""Guard fresh-clock recovery wiring and the bounded archive contract."""
from pathlib import Path
from loguru import logger

from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    """Stop on a missing production binding instead of accepting an empty test."""
    if not condition:
        raise AssertionError(message)


def profile(text, name):
    """Ensure corrections come from SNTP while room samples need a fresh sync."""
    start = text.index("\ntime:\n")
    end = text.index("\ndebug:\n", start)
    sync = text[start:end]
    require(
        sync.count("on_time_sync:") == 1, name + ": one sync trigger required"
    )
    require(
        sync.count("id(room_aggregate_history).confirm_time(") == 1,
        name + ": recovery must have one confirmed-time binding",
    )
    for token in (
        "now.is_valid()",
        "now.timestamp",
        "now.year",
        "now.day_of_year",
        "now.hour",
        "clock_sntp_synced",
    ):
        require(token in sync, name + ": missing sync input " + token)
    start = text.index("  - interval: 5min\n")
    end = text.index("  - interval: 5s\n", start)
    sample = text[start:end]
    require(
        sample.count("id(room_aggregate_history).record(") == 1,
        name + ": one aggregate sample call required",
    )
    require(
        "if (id(clock_sntp_synced)) {" in sample,
        name + ": retained clock must not enable aggregate sampling",
    )
    require(
        sample.count("id(room_history).record(") == 1,
        name + ": original 24H path must remain present",
    )
    require(
        text.count(".confirm_time(") == 1,
        name + ": recovery must not be triggered by ordinary samples",
    )


def storage_contract():
    """Pin data preservation and the resource-safe key lookup path."""
    directory = ROOT / "components/usp_room_aggregate_history"
    cpp = (directory / "usp_room_aggregate_history.cpp").read_text()
    marker = (directory / "marker-store.h").read_text()
    for token in (
        "load_from_key",
        "restore_archive_",
        "recovery_blocked_",
        "archive_full_",
        "restore_archive_bank_",
        "rebuild_archived_day_",
        "select_bank2_once",
    ):
        require(token in cpp, "missing recovery boundary: " + token)
    require(
        "nvs_erase" not in marker and "nvs_flash_erase" not in cpp,
        "recovery must preserve saved series",
    )
    require(
        "NVS_READONLY" in marker and "save_once" in marker,
        "read-only absence and immutable selection required",
    )
    require(
        "sizeof(LegacyMarker) == 20" in marker,
        "marker v1 ABI changed",
    )
    require(
        "sizeof(MarkerV2) == 32" in marker,
        "marker v2 ABI changed",
    )
    for token in (
        "VERSION_V1",
        "VERSION_V2",
        "second_cutoff_hour",
        "second_cutoff_day",
        "active_bank",
        "select_bank2_once",
        "write_and_verify",
    ):
        require(token in marker, "missing marker-v2 contract: " + token)
    require(
        cpp.count("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") == 2,
        "recovery gained a large extra allocation",
    )


def main():
    """Validate all profiles without contacting a device or building an image."""
    version = canonical_firmware_version()
    for name in (
        "ukraine-status-panel.yaml",
        "ukraine-status-panel-production.yaml",
        "ukraine-status-panel-public-first-flash.yaml",
    ):
        text = (ROOT / "firmware" / name).read_text()
        require(f'firmware_version: "{version}"' in text, name + ": version")
        profile(text, name)
    storage_contract()
    logger.info(
        "ROOM_CLOCK_RECOVERY_STATIC=PASS fresh_SNTP profiles3 archives2 marker_v1_v2 archive_preserved"
    )


if __name__ == "__main__":
    main()
