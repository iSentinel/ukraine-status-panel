#!/usr/bin/env python3
"""Guard the visible-surface map redraw batching/lazy-sync contract."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware" / "ukraine-status-panel.yaml"


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"MAP_RENDER_BATCHING_STATIC=FAIL missing {label}")


def block(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def check_home_and_visibility(text, refresh):
    """Require cache-first Home rendering plus the bounded legacy fallback."""
    require(refresh, "id: page_home", "Home visibility gate")
    require(refresh, "id: page_map_full", "fullscreen visibility gate")
    full_start = refresh.index("              id: page_map_full\n")
    home_refresh = refresh[:full_start]

    require(
        home_refresh,
        "usp_dashboard_map_cache::Cache::instance().refresh(",
        "Home PSRAM cache path",
    )
    require(home_refresh, "originals[26]", "Home cache original count")
    require(
        home_refresh,
        "id(dash_region_ua_77), id(dashboard_map_outline)",
        "Home cache outline ownership",
    )
    require(home_refresh, "count: 25", "Home fallback repeat count")
    require(
        home_refresh,
        "const size_t index = static_cast<size_t>(iteration);",
        "Home fallback one-region index",
    )
    require(home_refresh, "- delay: 24ms", "Home fallback inter-region yield")
    if "iteration) * 5U" in home_refresh or "begin + 5U" in home_refresh:
        raise SystemExit(
            "MAP_RENDER_BATCHING_STATIC=FAIL five-region Home burst returned"
        )

    bridge = (ROOT / "include/usp_dashboard_map_cache.h").read_text()
    require(bridge, "MALLOC_CAP_SPIRAM", "Home PSRAM-only allocation")
    require(bridge, "lv_obj_set_pos(image_, 24, 63)", "Home cache origin")
    require(bridge, "lv_obj_move_to_index(image_, 0)", "Home overlays above cache")


def check_fullscreen(text, refresh):
    full_start = refresh.index("              id: page_map_full\n")
    full_refresh = refresh[full_start:]
    require(full_refresh, "count: 9", "fullscreen adaptive repeat count")
    bridge = (ROOT / "include/usp_fullscreen_map_cache.h").read_text()
    require(full_refresh, "Cache::instance().refresh(", "optional cache path")
    require(full_refresh, "update_fallback(", "original fallback path")
    require(bridge, "batch == 0 ? 0 : 1 + (batch - 1) * 3", "fallback warm batch")
    require(bridge, "batch == 0 ? 1U : 3U", "fallback cold/warm width")
    require(
        bridge,
        "count > ORIGINAL_CAPACITY || !this->prepare_(parent)",
        "allocation fallback",
    )
    require(bridge, "MALLOC_CAP_SPIRAM", "PSRAM-only image allocation")
    require(bridge, "lv_obj_move_to_index(image_, 0)", "marker above cache")

    if full_refresh.count("- delay: 20ms") != 1:
        raise SystemExit("MAP_RENDER_BATCHING_STATIC=FAIL fullscreen batch delay")
    require(refresh, "cache=dashboard", "Home cache startup log")
    require(bridge, "Fullscreen map batch=%u changed=%u", "fullscreen fallback log")

    check_fullscreen_assets(text)


def check_fullscreen_assets(text):
    """Retain original map geometry, masks and outline constraints."""
    require(text, "id: fullscreen_map_origin", "fullscreen geometry origin anchor")
    require(
        text,
        "fullscreen-tiled/outline-tiles/outline-tile-00.png",
        "tiled outline asset",
    )
    require(
        text,
        "fullscreen-fast/masks-binary/region-ua-05.png",
        "outline-subtracted fullscreen masks",
    )
    if "fullscreen_map_base_fast" in text or "base-rgb565.png" in text:
        raise SystemExit(
            "MAP_RENDER_BATCHING_STATIC=FAIL RGB565 fullscreen base still active"
        )
    if (
        "id: fullscreen_map_outline_mask" in text
        or "id: fullscreen_map_outline\n" in text
    ):
        raise SystemExit(
            "MAP_RENDER_BATCHING_STATIC=FAIL legacy monolithic fullscreen outline active"
        )


def check_loads(text):
    home = block(text, "      - id: page_home\n", "      - id: page_weather\n")
    require(home, "on_load:", "Home on_load")
    require(home, "script.execute: refresh_dashboard_map_ui", "Home lazy sync")

    full_localize = block(
        text,
        "  - id: localize_page_map_full\n",
        "  - id: localize_page_weather_full\n",
    )
    require(
        full_localize,
        "script.execute: refresh_dashboard_map_ui",
        "fullscreen lazy sync",
    )

    qa_full = block(
        text,
        "    - action: usp_qa_show_map_full\n",
        "    - action: usp_qa_show_weather_full\n",
    )
    if "refresh_dashboard_map_ui" in qa_full:
        raise SystemExit("MAP_RENDER_BATCHING_STATIC=FAIL QA pre-show refresh")

    print(
        "MAP_RENDER_BATCHING_STATIC=PASS home_cache=1 home_fallback_width=1 home_fallback_cycles=25 fullscreen_cold=1 fullscreen_warm=3 fullscreen_cycles=9"
    )


def main():
    """Check both the cache integration and the unchanged fallback contract."""
    text = YAML.read_text(encoding="utf-8")
    refresh = block(
        text,
        "  - id: refresh_dashboard_map_ui\n",
        "  - id: schedule_dashboard_alert_refresh\n",
    )
    check_home_and_visibility(text, refresh)
    check_fullscreen(text, refresh)
    check_loads(text)


if __name__ == "__main__":
    main()
