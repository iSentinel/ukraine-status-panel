#!/usr/bin/env python3
"""Guard cache-first Home startup plus the staged low-memory fallback."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()


def need(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


need(CURRENT_FIRMWARE_MARKER in PROD, "wrong firmware version")
need("id: dashboard_map_startup_revealed" in PROD, "startup reveal latch missing")
need("id: reveal_dashboard_map_startup" in PROD, "startup reveal script missing")
need("usp_dashboard_map_cache::Cache::instance().active()" in PROD,
     "cache-first startup branch missing")
need("MAP_STARTUP_REVEAL_DONE cache=dashboard" in PROD,
     "cache startup completion telemetry missing")
need("count: 25" in PROD, "fallback region reveal count changed")
need("delay: 80ms" in PROD and "delay: 24ms" in PROD,
     "fallback reveal pacing changed")
need("MAP_STARTUP_REVEAL_DONE cache=fallback" in PROD,
     "fallback completion telemetry missing")
need("lv_obj_clear_flag(id(dashboard_map_outline), LV_OBJ_FLAG_HIDDEN)" in PROD,
     "fallback outline reveal missing")
need("script.execute: reveal_dashboard_home_startup" in PROD,
     "Home on_load does not start card reveal handoff")
need("script.execute: reveal_dashboard_map_startup" in PROD,
     "card handoff does not start map reveal")

for iso in (
    "05", "07", "09", "12", "14", "18", "21", "23", "26", "32",
    "35", "43", "46", "48", "51", "53", "56", "59", "61", "63",
    "65", "68", "71", "74", "77",
):
    anchor = f"id: dash_region_ua_{iso}\n                    hidden: true"
    need(anchor in PROD, f"dashboard region UA-{iso} not startup-hidden")
need("id: dashboard_map_outline\n                    hidden: true" in PROD,
     "dashboard outline not startup-hidden")
print(
    "LVGL_STARTUP_HANDOFF_STATIC=PASS cache_first=1 "
    "fallback_regions=25 fallback_reveal=1-per-frame fallback_outline=separate"
)
