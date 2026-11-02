#!/usr/bin/env python3
"""Guard cache-first Home map refresh and its one-region low-memory fallback."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()


def need(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


need(CURRENT_FIRMWARE_MARKER in PROD, "wrong firmware version")
start = PROD.index("  - id: refresh_dashboard_map_ui\n")
end = PROD.index(
    "      - if:\n          condition:\n            lvgl.page.is_showing:\n              id: page_map_full",
    start,
)
home = PROD[start:end]
need("usp_dashboard_map_cache::Cache::instance().refresh(" in home,
     "Home cache-first refresh missing")
need("id(dashboard_map_card), originals, 26" in home,
     "Home cache does not replace regions plus outline")
need("count: 25" in home, "Home fallback no longer iterates 25 regions")
need("const size_t index = static_cast<size_t>(iteration);" in home,
     "Home fallback is not one-region indexed")
need("delay: 24ms" in home, "Home fallback yield changed")
need("iteration) * 5U" not in home and "begin + 5U" not in home,
     "five-region fallback batching returned")
print(
    "DASHBOARD_MAP_SINGLE_REGION_REFRESH_STATIC=PASS "
    "cache_first=1 originals=26 fallback_regions=25 width=1 yield_ms=24"
)
