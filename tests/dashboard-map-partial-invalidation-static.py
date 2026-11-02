#!/usr/bin/env python3
"""Guard C-D6 partial Home/Clock map invalidation."""

from pathlib import Path

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]
CACHE = (ROOT / "firmware/include/usp_dashboard_map_cache.h").read_text()
RENDERER = (ROOT / "firmware/include/usp_dashboard_map_renderer.h").read_text()
PRIMITIVES = (ROOT / "firmware/include/usp_map_render_primitives.h").read_text()


def need(condition: bool, message: str) -> None:
    """Fail with a focused contract message."""
    if not condition:
        raise AssertionError(message)


def main() -> None:
    """Require renderer dirty bounds and translated LVGL area invalidation."""
    need("struct DirtyArea" in RENDERER, "renderer dirty area missing")
    need("resolve_run_span(" in RENDERER, "shared run span missing from dirty area")
    need("RunSpan span{}" in RENDERER, "dirty area span storage missing")
    need(
        "dirty->set_full()" in RENDERER,
        "initial/background full dirty missing",
    )
    need(
        "usp_map_render_primitives::paint_label(" in RENDERER
        and "dirty->include(offset, length)" in RENDERER
        and "observer(span.first, run.length);" in PRIMITIVES,
        "region runs do not contribute dirty bounds",
    )
    need("lv_obj_invalidate_area(target, &area);" in CACHE,
         "partial LVGL invalidation missing")
    need("coords.x1 + dirty.x1" in CACHE and "coords.y1 + dirty.y1" in CACHE,
         "dirty area is not translated into object coordinates")
    need("this->invalidate_(image_, dirty);" in CACHE,
         "Home cache image does not use dirty invalidation")
    need("this->invalidate_(target, dirty);" in CACHE,
         "Clock mirror does not use dirty invalidation")
    logger.info(
        "DASHBOARD_MAP_PARTIAL_INVALIDATION_STATIC=PASS "
        "renderer_bbox home clock full_fallback"
    )


if __name__ == "__main__":
    main()
