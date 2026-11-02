#!/usr/bin/env python3
"""Static contract for the repeated-Home localization invalidation gate."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    ROOT / "firmware" / "ukraine-status-panel.yaml",
    ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml",
)

GLOBAL = """  - id: dashboard_home_localized_language\n    type: uint8_t\n    restore_value: no\n    initial_value: \"255\"\n"""
ON_LOAD = """        on_load:\n          - if:\n              condition:\n                lambda: |-\n                  return id(dashboard_home_localized_language)\n                      != id(ui_language);\n              then:\n                - script.execute: localize_page_home\n          - script.execute: refresh_dashboard_map_ui\n"""
TAIL = """      - lambda: |-\n          id(dashboard_home_localized_language) = id(ui_language);\n      - script.execute: refresh_dashboard_ui\n"""
APPLY = """          then:\n            - script.execute: localize_page_home\n"""

for path in FILES:
    text = path.read_text()
    assert text.count(GLOBAL) == 1, f"{path.name}: localization global missing/duplicated"
    assert text.count(ON_LOAD) == 1, f"{path.name}: Home on_load gate missing/duplicated"
    assert text.count(TAIL) == 1, f"{path.name}: successful localization signature missing/duplicated"
    apply_start = text.index("  - id: apply_ui_language\n")
    apply_end = text.index("  - id: refresh_stack_i18n\n", apply_start)
    assert APPLY in text[apply_start:apply_end], f"{path.name}: language-change forced path lost"

print("PASS: Home localization is signature-gated on repeated entry and forced on language changes")
