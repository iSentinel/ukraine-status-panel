#!/usr/bin/env python3
"""Guard the cross-screen Operate hierarchy introduced by the 2026-10-21 design review."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
MASTER = (ROOT / "design-system/ukraine-status-panel/MASTER.md").read_text(encoding="utf-8")


def need(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def id_block(widget_id: str, size: int = 600) -> str:
    start = YAML.index(f"id: {widget_id}")
    return YAML[start:start + size]


def main() -> int:
    need("Operate / monitoring dashboard" in MASTER, "Operate design mode missing")
    for widget_id in ("weather_title_label", "i18n_023", "i18n_038", "i18n_051"):
        block = id_block(widget_id, 220)
        need("text_font: usp_i18n_28" in block,
             f"{widget_id} competes with headline data")
        need("text_font: usp_i18n_36" not in block,
             f"{widget_id} returned to hero scale")

    # The primary data can still lead at 36px: this is deliberate hierarchy.
    for widget_id in ("i18n_040", "history_count_label"):
        need("text_font: usp_i18n_36" in id_block(widget_id, 220),
             f"{widget_id} lost answer-figure hierarchy")

    for signature in (
        "id: alert_map_touch_surface",
        "width: 450\n              height: 180",
        "width: 496\n              height: 180",
        "width: 968\n              height: 180",
    ):
        start = YAML.index(signature)
        block = YAML[start:start + 420]
        need("radius: 16" in block, f"panel radius drift near {signature}")
        need("bg_color: 0x0C1828" in block, f"panel surface drift near {signature}")

    for page_id in ("page_weather", "page_weather_full", "page_map", "page_power",
                    "page_history", "page_diagnostics", "page_room"):
        start = YAML.index(f"      - id: {page_id}\n")
        end = YAML.find("      - id: page_", start + 20)
        block = YAML[start:] if end < 0 else YAML[start:end]
        need("radius: 18" not in block, f"legacy 18px panel radius remains on {page_id}")
        need("bg_color: 0x101E30" not in block,
             f"legacy secondary top-level surface remains on {page_id}")

    print("DESIGN_SYSTEM_OPERATE_STATIC=PASS titles=4 answer_figures=2 panels=flat radius=16")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"DESIGN_SYSTEM_OPERATE_STATIC=FAIL {exc}")
        raise SystemExit(1)
