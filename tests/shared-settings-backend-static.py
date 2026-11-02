#!/usr/bin/env python3
"""Guard the shared settings mutation layer used by touch/API/Web UI."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware" / "ukraine-status-panel.yaml"


def require(text: str, needle: str, label: str) -> None:
    pattern = r"\s+".join(re.escape(part) for part in needle.split())
    if re.search(pattern, text) is None:
        raise AssertionError(f"missing {label}: {needle!r}")


def main() -> int:
    text = YAML.read_text()

    for script_id in (
        "set_ui_language",
        "set_alert_palette",
        "set_outage_group",
        "set_selected_region",
        "sync_backlight_setting_ui",
    ):
        require(text, f"  - id: {script_id}\n", f"shared script {script_id}")

    # Mutation ownership: boot may sanitize palette, but runtime writes must
    # live in the shared setting layer only.
    if text.count("id(ui_language) = static_cast<uint8_t>(language)") != 1:
        raise AssertionError("ui_language runtime mutation is not single-owner")
    language_start = text.index("  - id: set_ui_language\n")
    language_end = text.index("\n  - id: set_alert_palette\n", language_start)
    language_setter = text[language_start:language_end]
    require(
        language_setter,
        "return id(ui_language) !=\n                        static_cast<uint8_t>(language);",
        "idempotent language guard",
    )
    if text.count("id(ui_alert_palette) = static_cast<uint8_t>(mode)") != 1:
        raise AssertionError("ui_alert_palette runtime mutation is not single-owner")
    if text.count("id(outage_client).set_group(group)") != 1:
        raise AssertionError("outage group runtime mutation is not single-owner")
    if text.count("id(region_settings).select_region(next)") != 1:
        raise AssertionError("user region mutation is not single-owner")

    # Backlight Light entity is the source of truth for touch/API/future Web.
    require(text, "    on_state:\n", "backlight on_state")
    require(
        text,
        "id(backlight).remote_values.get_brightness() * 100.0f",
        "backlight remote-value sync",
    )
    slider_start = text.index("          - slider:\n              id: backlight_slider")
    slider_end = text.index("\n\nscript:\n", slider_start)
    slider = text[slider_start:slider_end]
    if "id(ui_backlight_percent) =" in slider:
        raise AssertionError("backlight slider still owns a duplicate state write")

    require(
        text,
        "    - action: usp_qa_dump_shared_settings_134\n",
        "shared settings QA snapshot",
    )
    require(text, "SETTINGS134 language=%u", "settings snapshot log")

    # QA render entities must never surface in the user-facing Web entity list.
    for entity_id in (
        "qa_render_air_alert_low_button",
        "qa_render_air_alert_button",
        "qa_render_explosion_button",
        "qa_render_semantic_mixed_button",
    ):
        marker = f"    id: {entity_id}\n    internal: true\n"
        require(text, marker, f"internal QA entity {entity_id}")

    # Touchscreen controls and Native API actions delegate to shared setters.
    for needle in (
        "id: set_ui_language\n                          language: 0",
        "id: set_ui_language\n                          language: 1",
        "id: set_ui_language\n                          language: 2",
        "id: set_alert_palette\n                    mode: 0",
        "id: set_alert_palette\n                    mode: 1",
        "id: set_alert_palette\n                    mode: 2",
        "id: set_outage_group\n            group: !lambda return group;",
        "id(set_selected_region).execute(static_cast<int>(region_id));",
    ):
        require(text, needle, "delegated setting path")

    print("SHARED_SETTINGS_BACKEND_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"SHARED_SETTINGS_BACKEND_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
