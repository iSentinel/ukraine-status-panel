#!/usr/bin/env python3
"""Guard the bounded, truthful, low-chrome fullscreen NEPTUN threat layer."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(
    encoding="utf-8"
)
HELPER = (ROOT / "include/usp_threat_icons_ui.h").read_text(encoding="utf-8")
POINT = (ROOT / "components/usp_neptun/neptun_threat_point.h").read_text(
    encoding="utf-8"
)
PARSER = (ROOT / "components/usp_neptun/neptun_threat_parser.cpp").read_text(
    encoding="utf-8"
)


def need(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def widget_block(widget_id: str) -> str:
    start = YAML.index(f"id: {widget_id}")
    end = YAML.find("\n                - ", start)
    return YAML[start:] if end < 0 else YAML[start:end]


def main() -> int:
    for index in range(12):
        marker = widget_block(f"fullscreen_threat_marker_{index}")
        heading = widget_block(f"fullscreen_threat_heading_{index}")
        need("width: 28" in marker and "height: 24" in marker,
             f"marker {index} geometry changed")
        need("bg_color: 0x07111F" in marker,
             f"marker {index} is no longer low-chrome")
        legible = (
            "border_width: 2" in marker
            and "text_font: usp_i18n_18" in marker
        )
        need(legible, f"marker {index} legibility contract changed")
        need("clickable: false" in marker and "scrollable: false" in marker,
             f"marker {index} can steal map gestures")
        need("hidden: true" in marker and "hidden: true" in heading,
             f"threat slot {index} must start hidden")
        need("clickable: false" in heading and "line_width: 2" in heading,
             f"heading {index} interaction/style changed")

    need('return approximate ? "~U" : "U"' in HELPER,
         "approximate quality is not visible in the marker label")
    for token in ("MISSILE", "BALLISTIC", "KAB", "MIG31K"):
        need(token in HELPER, f"missing threat kind {token}")
    need("THREAT_POINT_CAPACITY = 12" in POINT, "bounded capacity changed")
    need("THREAT_POINT_TTL_SECONDS = 20U * 60U" in POINT,
         "threat icon TTL changed")
    need('"areaOnly"' in PARSER and "object_area_only_" in PARSER and
         "object_advisory_" in PARSER,
         "area/advisory filtering contract missing")
    heading_parse = (
        '"presumptiveCourse"' in PARSER
        and "object_presumptive_course_" in PARSER
    )
    need(heading_parse, "presumptive heading contract missing")

    refresh = YAML[YAML.index("  - id: refresh_fullscreen_threat_icons\n"):
                   YAML.index("  - id: refresh_neptun_ui\n")]
    need("!id(neptun_client).valid() || id(neptun_client).stale()" in refresh,
         "stale/invalid points are not hidden")
    need("last_fetch_ok()" not in refresh,
         "single failed fetch must retain fresh cached threat points")
    need("std::min<size_t>(snapshot.threat_point_count, 12)" in refresh,
         "renderer capacity is not bounded")
    need("lv_obj_set_style_border_color" in refresh and
         "lv_obj_set_style_text_color" in refresh,
         "semantic color is not limited to marker data marks")
    need("lv_obj_set_style_line_color" in refresh,
         "measured heading does not share threat semantics")
    need("esphome::lvgl::LvLineType *headings[]" in refresh,
         "LVGL line wrappers are not typed correctly")
    for call in ("lv_obj_add_flag(headings[index]->obj",
                 "lv_obj_set_pos(headings[index]->obj",
                 "lv_line_set_points(headings[index]->obj",
                 ("lv_obj_set_style_line_color(\n"
                  "                headings[index]->obj"),
                 "lv_obj_clear_flag(headings[index]->obj"):
        need(call in refresh, f"line wrapper object usage missing: {call}")
    need(
        "trail" not in refresh.lower(),
        "trails/extrapolation entered renderer",
    )
    refresh_calls = YAML.count(
        "script.execute: schedule_fullscreen_threat_icons_refresh"
    )
    need(
        "schedule_fullscreen_threat_icons_refresh" in YAML
        and refresh_calls >= 2,
        "threat icons are not refreshed on data + fullscreen entry",
    )

    print(
        "NEPTUN_THREAT_ICONS_STATIC=PASS slots=12 dark_fill=1 "
        "semantic_edge=1 touch_passthrough=1 motion=0"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"NEPTUN_THREAT_ICONS_STATIC=FAIL {exc}")
        raise SystemExit(1)
