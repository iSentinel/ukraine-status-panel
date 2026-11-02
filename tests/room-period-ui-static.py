#!/usr/bin/env python3
"""Guard the bounded Room 24h/7d/30d/1y selector and aggregate wiring."""

from pathlib import Path

from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
PRODUCTION = (ROOT / "firmware/ukraine-status-panel-production.yaml").read_text()
MODEL = (ROOT / "include/usp_room_chart_model.h").read_text()
PERIOD = (ROOT / "include/usp_room_period_chart.h").read_text()
TEXT = (ROOT / "include/usp_room_period_text.h").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def room_block(text: str) -> str:
    start = text.index("      - id: page_room\n")
    end = text.index("\n      - id: page_alarms\n", start)
    return text[start:end]


def selector_contract(text: str, profile: str) -> None:
    room = room_block(text)
    controls = (
        ("room_period_24h_button", 300, "24H", 0),
        ("room_period_7d_button", 392, "7D", 1),
        ("room_period_30d_button", 484, "30D", 2),
        ("room_period_1y_button", 576, "1Y", 3),
    )
    for ident, x, label, value in controls:
        start = room.index(f"id: {ident}")
        fragment = room[start:start + 420]
        require(f"x: {x}" in fragment, f"{profile}: {ident} x")
        require("width: 84" in fragment and "height: 48" in fragment,
                f"{profile}: {ident} touch target")
        require(f'text: "{label}"' in fragment, f"{profile}: {ident} text")
        require(f"id(room_history_period) = {value}" in fragment,
                f"{profile}: {ident} action")
        require("script.execute: refresh_room_graph_ui" in fragment,
                f"{profile}: {ident} refresh")
    require("id: room_period_label" not in room,
            f"{profile}: obsolete period label remains")


def graph_contract(text: str, profile: str) -> None:
    start = text.index("  - id: refresh_room_graph_ui\n")
    end = text.index("\n  - id:", start + 10)
    graph = text[start:end]
    require("usp_room_chart::build(*id(room_history)" in graph,
            f"{profile}: 24h source missing")
    require("usp_room_period_chart::build(*id(room_aggregate_history)" in graph,
            f"{profile}: aggregate source missing")
    require("RoomAggregateHistory::local_day_key" in graph,
            f"{profile}: local-day boundary missing")
    require("id(room_graph_valid_count) = work->model.valid_count" in graph,
            f"{profile}: 32-bit valid counter publish missing")
    require("id(room_graph_gap_count) = work->model.gap_count" in graph,
            f"{profile}: 32-bit gap counter publish missing")
    require(graph.count("allocate_workspace(") == 1,
            f"{profile}: graph must own one workspace")
    require("usp_room_period_text::view" in graph,
            f"{profile}: period localization missing")
    require("period_buttons[]" in graph and "0x1D4ED8" in graph,
            f"{profile}: selected button style missing")


def main() -> None:
    """Keep storage unchanged and expose all four periods in every profile."""
    require("MAX_POINTS = 372" in MODEL, "372-point chart model missing")
    require("uint32_t valid_count" in MODEL and "uint32_t gap_count" in MODEL,
            "long-period counters are not 32-bit")
    for token in ("7U * 24U", "30U", "366U", "MAX_SEGMENTS"):
        require(token in PERIOD, f"period helper missing {token}")
    require("resolve_end" in PERIOD, "clock-untrusted restore fallback missing")
    require("VIEWS[4][3]" in TEXT, "period text matrix missing")
    require("Aggregated history" in TEXT, "persistent aggregate note missing")
    version = canonical_firmware_version()
    for profile, text in (
        ("canonical", YAML), ("public", PUBLIC), ("production", PRODUCTION)
    ):
        require(f'firmware_version: "{version}"' in text,
                f"{profile}: wrong version")
        selector_contract(text, profile)
        graph_contract(text, profile)
        require("id: room_history_period" in text,
                f"{profile}: period state missing")
        require("restore_value: no" in text[text.index("id: room_history_period"):
                                             text.index("id: room_history_period") + 180],
                f"{profile}: period selection should not write flash")
    header = (ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.h").read_text()
    cpp = (ROOT / "components/usp_room_aggregate_history/usp_room_aggregate_history.cpp").read_text()
    for token in ("hourly_sample", "daily_sample", "hourly_count", "daily_count",
                  "local_day_key"):
        require(token in header, f"aggregate presentation API missing {token}")
    require("PAGE_VERSION = 2" in cpp and "last_raw_bucket_plus_one" in cpp,
            "aggregate v2 cursor contract missing")
    require("LegacyHourPage" in cpp and "LegacyDayPage" in cpp,
            "aggregate v1 migration reader missing")
    print("ROOM_PERIOD_UI_STATIC=PASS periods4 touch84x48 sources24h+aggregate model372 counters32 backend_api+cursor_v2")


if __name__ == "__main__":
    main()
