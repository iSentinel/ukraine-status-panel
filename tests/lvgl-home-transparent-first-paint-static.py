#!/usr/bin/env python3
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT=Path(__file__).resolve().parents[1]
TEXT=(ROOT/"firmware/ukraine-status-panel.yaml").read_text()

def need(ok,msg):
    if not ok:
        raise SystemExit(f"LVGL_HOME_TRANSPARENT_FIRST_PAINT_STATIC=FAIL {msg}")

need(CURRENT_FIRMWARE_MARKER in TEXT,"wrong firmware version")
need("    bg_color: 0x07111F\n    bg_opa: 100%" in TEXT,"global opaque background changed")
start=TEXT.index("      - id: page_home\n")
end=TEXT.index("      - id: page_weather\n",start)
home=TEXT[start:end]
need("        bg_opa: 100%\n" in home,"Home opaque root not restored")
for page in ("page_weather","page_map_full","page_weather_full","page_map","page_power","page_history","page_settings","page_diagnostics"):
    pos=TEXT.index(f"      - id: {page}\n")
    nxt=TEXT.find("      - id: page_",pos+1)
    block=TEXT[pos:nxt if nxt!=-1 else len(TEXT)]
    need("        bg_opa: 0%\n" not in block,f"transparent experiment leaked into {page}")
print("LVGL_HOME_TRANSPARENT_FIRST_PAINT_STATIC=PASS experiment_reverted=1 opaque_root=1")
