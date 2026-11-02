#!/usr/bin/env python3
"""Guard the bounded M7 aggregate room-history backend wiring and scope."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.h").read_text()
CPP = (ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.cpp").read_text()
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
PRODUCTION = (ROOT / "firmware/ukraine-status-panel-production.yaml").read_text()
ROOM_UI = (ROOT / "include/usp_room_ui_text.h").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def component_contract() -> None:
    """Pin capacity, persistence and PSRAM-only activation boundaries."""
    for token in (
        "HOUR_RING_CAPACITY = 32U * 24U",
        "DAY_RING_CAPACITY = 12U * 31U",
        "HOUR_PAGE_SLOTS = 32",
        "DAY_PAGE_SLOTS = 13",
        "CHECKPOINT_MS = 6U * 60U * 60U * 1000U",
        "NVS_PAYLOAD_BYTES",
        "static_assert(sizeof(Aggregate) == 24",
    ):
        require(token in HEADER, f"missing backend contract: {token}")
    require(CPP.count("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") == 2,
            "aggregate storage must use exactly two PSRAM allocations")
    require("trying internal heap" not in CPP,
            "large aggregate storage gained an internal-heap fallback")
    require("Aggregate history dormant; waiting for first valid room sample" in CPP,
            "absent-sensor dormant contract missing")
    require("global_preferences->sync()" in CPP, "page persistence sync missing")
    require("if (this->save_failed_)" in CPP and "retry_due_(now)" in CPP,
            "failed persistence retry boundary missing")


def yaml_contract(text: str, profile: str) -> None:
    """Require both old 24h and new aggregate paths in every generated profile."""
    require(text.count("      - usp_room_aggregate_history\n") == 1,
            f"{profile}: aggregate component registration missing/duplicated")
    require(text.count("\nusp_room_aggregate_history:\n") == 1,
            f"{profile}: aggregate config block missing/duplicated")
    require(text.count("id(room_aggregate_history).record(") == 1,
            f"{profile}: aggregate record binding missing/duplicated")
    require(text.count("id(room_history).record(") == 1,
            f"{profile}: existing 24h room history binding changed")
    call = text[text.index("id(room_aggregate_history).record("):]
    call = call[:call.index(");") + 2]
    for token in ("now.timestamp", "now.year", "now.day_of_year", "now.hour",
                  "environment_temperature", "environment_humidity", "valid"):
        require(token in call, f"{profile}: aggregate call lacks {token}")


def scope_contract() -> None:
    """Keep .387 backend-only; week/month/year UI is a later bounded change."""
    require("24h history" in ROOM_UI, "existing 24h room UI changed")
    for label in ("7d history", "31d history", "366d history"):
        require(label not in ROOM_UI, f".387 unexpectedly added UI: {label}")
    require("room_aggregate_history" not in ROOM_UI,
            "aggregate backend leaked into presentation catalog")


def main() -> None:
    """Run static contracts without firmware build or device access."""
    component_contract()
    yaml_contract(YAML, "canonical")
    yaml_contract(PUBLIC, "public")
    yaml_contract(PRODUCTION, "production")
    scope_contract()
    print(
        "ROOM_AGGREGATE_STATIC=PASS hourly768 daily372 pages32+13 "
        "checkpoint6h psram_only dormant_absent ui24h_unchanged"
    )


if __name__ == "__main__":
    main()
