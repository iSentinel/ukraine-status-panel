#!/usr/bin/env python3
"""Guard idempotent, visible-surface-only palette redraw paths."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware" / "ukraine-status-panel.yaml"


def require_palette_path(block: str, label: str) -> None:
    """Validate one palette mutation block without touching state-render caches."""
    forbidden = (
        "alert_render_cache_valid_mask",
        "alert_render_inflight_mask",
        "script.execute: alert_render_worker",
    )
    for token in forbidden:
        if token in block:
            raise RuntimeError(f"{label} still mutates state-render path: {token}")
    for token in (
        "script.execute: refresh_alert_palette_controls",
        "script.execute: refresh_alert_map_palette_ui",
        "script.execute: refresh_dashboard_map_ui",
    ):
        if token not in block:
            raise RuntimeError(f"{label} missing palette surface action: {token}")


def main() -> None:
    """Reject palette changes that force or duplicate a state redraw."""
    text = CONFIG.read_text(encoding="utf-8")

    global_block = (
        "  - id: alert_map_applied_palette\n"
        "    type: uint8_t\n"
        "    restore_value: no\n"
        "    initial_value: \"0\""
    )
    if global_block not in text:
        raise RuntimeError("primary-map applied-palette marker missing")

    setter_start = text.index("  - id: set_alert_palette\n")
    setter_end = text.index("\n  - id: set_outage_group\n", setter_start)
    setter = text[setter_start:setter_end]
    if "return id(ui_alert_palette) !=" not in setter:
        raise RuntimeError("shared palette setter is not idempotent")
    if "id(ui_alert_palette) = static_cast<uint8_t>(mode)" not in setter:
        raise RuntimeError("shared palette setter does not own palette mutation")
    require_palette_path(setter, "shared palette setter")

    qa_start = text.index("    - action: usp_qa_set_alert_palette_114\n")
    qa_end = text.index("    - action: usp_qa_dump_alert_palette_114\n", qa_start)
    qa = text[qa_start:qa_end]
    if "id: set_alert_palette" not in qa or "mode: !lambda return mode;" not in qa:
        raise RuntimeError("QA palette action does not delegate to shared setter")
    if "id(ui_alert_palette) =" in qa:
        raise RuntimeError("QA palette action still mutates palette directly")

    for button_id, mode in (
        ("palette_standard_button", 0),
        ("palette_accessible_button", 1),
        ("palette_mono_button", 2),
    ):
        start = text.index(f"              id: {button_id}\n")
        end = text.index("\n          - button:", start)
        button = text[start:end]
        if "id: set_alert_palette" not in button or f"mode: {mode}" not in button:
            raise RuntimeError(f"{button_id} does not delegate to shared setter")
        if "id(ui_alert_palette) =" in button:
            raise RuntimeError(f"{button_id} still mutates palette directly")

    script_start = text.index("  - id: refresh_alert_map_palette_ui\n")
    script_end = text.index("\n  - id: alert_render_worker\n", script_start)
    script = text[script_start:script_end]
    required = (
        "lvgl.page.is_showing:\n              id: page_map",
        "id(alert_map_applied_palette) !=",
        "count: 13",
        "static_cast<size_t>(iteration) * 2U",
        "begin + 2U",
        "delay: 20ms",
        "id(alert_region_ua_05)",
        "id(alert_region_ua_77)",
        "id(alert_render_states)[index]",
        "id(alert_map_applied_palette) =",
    )
    for token in required:
        if token not in script:
            raise RuntimeError(f"main map palette batching missing: {token}")
    for stale_token in ("state_cache", "cache_ready", "palette_cache"):
        if stale_token in script:
            raise RuntimeError(f"palette renderer still caches state: {stale_token}")

    page_start = text.index("      - id: page_map\n")
    page_end = text.index("        widgets:\n", page_start)
    page_load = text[page_start:page_end]
    if "script.execute: refresh_alert_map_palette_ui" not in page_load:
        raise RuntimeError("page_map does not lazy-sync its palette surface")

    worker_start = text.index("  - id: alert_render_worker\n")
    worker_end = text.index("\n\n  - id: qa_render_air_alert_low", worker_start)
    worker = text[worker_start:worker_end]
    if worker.count("script.execute: refresh_dashboard_map_ui") != 1:
        raise RuntimeError("state render worker must retain one dashboard map update")
    if worker.count("script.execute: schedule_dashboard_alert_refresh") != 1:
        raise RuntimeError("state render worker must retain one dashboard alert refresh")
    if "id(alert_render_changed_count) == 25U" not in worker:
        raise RuntimeError("full state render does not sync applied palette marker")
    if "id(alert_map_applied_palette) =" not in worker:
        raise RuntimeError("state renderer never updates applied palette marker")

    print("PALETTE_REDRAW_PATH_STATIC=PASS")


if __name__ == "__main__":
    main()
