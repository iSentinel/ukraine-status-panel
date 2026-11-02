#!/usr/bin/env python3
"""Guard the Home dashboard PSRAM map cache and its low-memory fallback."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC_PATH = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
PUBLIC = PUBLIC_PATH.read_text()
CACHE = (ROOT / "firmware/include/usp_dashboard_map_cache.h").read_text()
RUNS = (ROOT / "firmware/include/usp_dashboard_map_runs.h").read_text()


def require(ok: bool, message: str) -> None:
    if not ok:
        raise RuntimeError(message)


def check_yaml(text: str, label: str) -> None:
    """Require cache-first Home rendering with the preserved fallback."""
    for token in (
        "include/usp_dashboard_map_runs.h",
        "include/usp_dashboard_map_renderer.h",
        "include/usp_dashboard_map_cache.h",
        "lv_obj_t *originals[26]",
        "id(dash_region_ua_77), id(dashboard_map_outline)",
        "usp_dashboard_map_cache::Cache::instance().refresh(",
        "id(dashboard_map_card), originals, 26",
        "count: 25",
        "MAP_STARTUP_REVEAL_DONE cache=dashboard",
        "MAP_STARTUP_REVEAL_DONE cache=fallback",
    ):
        require(token in text, f"{label}: missing {token}")


def main() -> None:
    check_yaml(PRIVATE, "private")
    check_yaml(PUBLIC, "public")
    for token in (
        "lv_obj_set_pos(image_, 24, 63)",
        "rgb565(0x0C1828)",
        "rgb565(0x91A8B2)",
        "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",
        "lv_obj_move_to_index(image_, 0)",
        "Dashboard PSRAM cache unavailable; keeping original map images",
    ):
        require(token in CACHE, f"cache contract missing {token}")
    changed_pos = CACHE.index(
        "const bool changed = renderer_.update(colors, &dirty);"
    )
    noop_pos = CACHE.index("if (!changed)", changed_pos)
    drop_pos = CACHE.index("lv_image_cache_drop(&descriptor_);", noop_pos)
    invalidate_pos = CACHE.index(
        "this->invalidate_(image_, dirty);", drop_pos
    )
    require(
        changed_pos < noop_pos < drop_pos < invalidate_pos,
        "unchanged cache must return before cache drop/dirty invalidation",
    )
    require(
        "lv_obj_invalidate_area(target, &area);" in CACHE,
        "changed region must use bounded LVGL invalidation",
    )
    require(
        "coords.x1 + dirty.x1" in CACHE and "coords.y1 + dirty.y1" in CACHE,
        "dirty bounds must be translated to object coordinates",
    )
    require(
        "DASHBOARD_CACHE_UPDATE seq=%u changed=YES" in CACHE,
        "cache update log must only describe real pixel changes",
    )
    require(
        "WIDTH = 476, HEIGHT = 271" in RUNS,
        "dashboard run dimensions drifted",
    )
    require("STARTS[28]" in RUNS, "dashboard run label sentinel missing")
    print(
        "DASHBOARD_MAP_CACHE_STATIC=PASS size=476x271 bytes=257992 "
        "labels=26 psram=1 fallback=25 partial_bbox=1 unchanged_noop=1"
    )


if __name__ == "__main__":
    main()
