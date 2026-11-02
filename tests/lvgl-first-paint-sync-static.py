#!/usr/bin/env python3
# Guard the deterministic LVGL first-paint barrier used by Home startup.
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
HDR = (ROOT / "components/lvgl/lvgl_esphome.h").read_text()
CPP = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()

def need(ok: bool, message: str) -> None:
    if not ok:
        raise SystemExit(f"LVGL_FIRST_PAINT_SYNC_STATIC=FAIL {message}")

need(CURRENT_FIRMWARE_MARKER in YAML, "wrong firmware version")
need("lvgl:\n  - id: panel_lvgl\n" in YAML, "LVGL component ID missing")
need("bool has_completed_first_loop() const" in HDR, "first-loop getter missing")
need("bool first_loop_completed_{false};" in HDR, "first-loop latch missing")
handler = CPP.index("lv_timer_handler();")
latch = CPP.index("this->first_loop_completed_ = true;", handler)
need(latch > handler, "first-loop latch set before handler returns")
start = YAML.index("  - id: reveal_dashboard_home_startup\n")
end = YAML.index("  - id: reveal_dashboard_map_startup\n", start)
block = YAML[start:end]
need("wait_until:" in block, "wait_until barrier missing")
need("id(panel_lvgl).has_completed_first_loop()" in block,
     "Home startup does not query LVGL first-loop latch")
need("timeout: 2s" in block, "first-paint barrier lacks bounded timeout")
ready = block.index("HOME_STARTUP_FIRST_PAINT_READY")
reveal = block.index("HOME_STARTUP_REVEAL card=map")
need(ready < reveal, "map reveals before first-paint telemetry")
need("delay: 80ms" not in block, "fixed timing guess still gates first card")
print("LVGL_FIRST_PAINT_SYNC_STATIC=PASS barrier=lv_timer_handler timeout_s=2 fixed_guess=0")
