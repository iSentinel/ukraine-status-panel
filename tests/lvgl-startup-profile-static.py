#!/usr/bin/env python3
"""Guard the temporary cold-boot LVGL timing probes used by hardware QA."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()
for token in (
    "LVGL_REFRESH_PROFILE",
    "LVGL_FLUSH_SUBMIT_PROFILE",
    "LVGL_FLUSH_WAIT_PROFILE",
    "observe(now, sample.handler_ms, 50, 8)",
    "observe(millis(), submit_elapsed_ms, 25, 6)",
    "observe(millis(), wait_elapsed_ms, 25, 6)",
):
    if token not in CPP:
        raise SystemExit(f"LVGL startup profile contract missing: {token}")
if (
    "startup_loop_profile_seq" not in CPP
    or "startup_flush_wait_profile_seq" not in CPP
):
    raise SystemExit("LVGL startup profile must remain startup-bounded")
print("LVGL_STARTUP_PROFILE_STATIC=PASS loop=1 submit=1 wait=1 bounded=1")
