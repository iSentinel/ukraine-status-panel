#!/usr/bin/env python3
# Guard staged Home-card startup rendering introduced in 0.115.196.
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
TEXT = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()

def need(ok: bool, message: str) -> None:
    if not ok:
        raise SystemExit(f"LVGL_HOME_CARD_HANDOFF_STATIC=FAIL {message}")

need(CURRENT_FIRMWARE_MARKER in TEXT, "wrong firmware version")
need("id: dashboard_home_startup_revealed" in TEXT, "home reveal latch missing")
need("script.execute: reveal_dashboard_home_startup" in TEXT, "Home on_load handoff missing")
start = TEXT.index("  - id: reveal_dashboard_home_startup\n")
end = TEXT.index("  - id: reveal_dashboard_map_startup\n", start)
block = TEXT[start:end]
for card in ("dashboard_map_card", "dashboard_alerts_card", "dashboard_power_card", "dashboard_history_card", "dashboard_outage_card"):
    need(f"lv_obj_clear_flag(id({card}), LV_OBJ_FLAG_HIDDEN)" in block, f"{card} reveal missing")
    need(f"id: {card}\n              hidden: true" in TEXT, f"{card} not startup-hidden")
need(block.count("delay: 32ms") == 4, "card pacing changed")
need("delay: 80ms" not in block, "fixed first-paint timing guess returned")
need("delay: 40ms" in block, "map handoff boundary pacing changed")
need("wait_until:" in block and "id(panel_lvgl).has_completed_first_loop()" in block,
     "first-paint barrier missing")
need("HOME_STARTUP_FIRST_PAINT_READY" in block, "first-paint telemetry missing")
need("HOME_STARTUP_REVEAL_DONE" in block, "completion telemetry missing")
need(block.rfind("script.execute: reveal_dashboard_map_startup") > block.find("HOME_STARTUP_REVEAL_DONE"), "map reveal starts before cards complete")
print("LVGL_HOME_CARD_HANDOFF_STATIC=PASS cards=5 yield_ms=32 map_after_cards=1")
