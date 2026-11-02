#!/usr/bin/env python3
"""Keep the Home dashboard refresh split into cooperative bounded phases."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXT = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()

start = TEXT.index("  - id: refresh_dashboard_ui\n")
end = TEXT.index("  - id: refresh_selected_neptun_ui\n", start)
block = TEXT[start:end]

children = [
    "refresh_dashboard_alert_ui",
    "refresh_dashboard_power_ui",
    "refresh_dashboard_weather_ui",
    "refresh_dashboard_outage_ui",
    "refresh_dashboard_history_ui",
]
for child in children:
    token = f"script.execute: {child}"
    if block.count(token) != 1:
        raise SystemExit(f"dashboard cooperative child contract missing: {child}")

if block.count("delay: 2ms") < len(children):
    raise SystemExit("dashboard refresh no longer yields between bounded phases")

# The coordinator must not regress into direct widget mutations. Child scripts own them.
for forbidden in ("lvgl.label.update:", "lv_obj_", "dashboard_alert_count_label", "dashboard_now_marker"):
    if forbidden in block:
        raise SystemExit(f"monolithic dashboard mutation returned: {forbidden}")

print("DASHBOARD_COOPERATIVE_REFRESH_STATIC=PASS phases=5 yield_ms=2 direct_mutations=0")
