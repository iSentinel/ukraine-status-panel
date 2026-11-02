#!/usr/bin/env python3
"""Static contract for the internal Home render-isolation QA mask."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FILES=(ROOT/"firmware/ukraine-status-panel.yaml",ROOT/"firmware/ukraine-status-panel-public-first-flash.yaml")
TOKENS=(
    "- action: usp_qa_home_render_mask",
    "hide_mask: int",
    "id(dashboard_map_card), (mask & 0x01U)",
    "id(dashboard_alerts_card), (mask & 0x02U)",
    "id(dashboard_power_card), (mask & 0x04U)",
    "id(dashboard_history_card), (mask & 0x08U)",
    "id(dashboard_outage_card), (mask & 0x10U)",
    "HOME_RENDER_MASK=%lu",
)
for path in FILES:
    text=path.read_text()
    if "public-first-flash" in path.name:
        assert all(token not in text for token in TOKENS), "Public profile exposes Home QA injection"
        continue
    for token in TOKENS:
        assert text.count(token)==1, f"{path.name}: missing/duplicate {token!r}"
    assert "hide_mask < 0 || hide_mask > 31" in text, f"{path.name}: mask range guard missing"
print("PASS: Home render QA mask covers map/alerts/power/history/outage with reversible mask=0 semantics")
