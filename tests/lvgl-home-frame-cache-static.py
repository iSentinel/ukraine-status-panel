#!/usr/bin/env python3
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()
HDR = (ROOT / "components/lvgl/lvgl_esphome.h").read_text()
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()

def need(cond, msg):
    if not cond:
        raise SystemExit("FAIL: " + msg)

for text, name in ((PROD, "production"), (PUBLIC, "public")):
    need(CURRENT_FIRMWARE_MARKER in text, f"{name} version")
    for action in ("usp_qa_frame_cache_capture", "usp_qa_frame_cache_present", "usp_qa_frame_cache_present_dma", "usp_qa_frame_cache_release"):
        need((f"- action: {action}" in text) == (name == "production"), f"{name} unexpected QA action policy: {action}")
need("qa_capture_native_frame" in HDR and "qa_present_cached_native_frame" in HDR and "qa_present_cached_native_frame_dma" in HDR, "QA API declarations")
need("last_presented_frame_ = color_p" in CPP, "flush must record exact submitted native buffer")
need("last_presented_frame_complete_ = frame_complete" in CPP, "completion latch missing")
need("heap_caps_aligned_alloc(64, frame_bytes" in CPP, "lazy aligned PSRAM cache missing")
need("std::memcpy(target, this->qa_frame_cache_" in CPP, "CPU cache-to-inactive-native control copy missing")
need("esp_async_memcpy_install_gdma_ahb" in CPP and "esp_async_memcpy(" in CPP, "AHB-GDMA PSRAM copy probe missing")
need("FRAME_CACHE_DMA_PRESENT" in CPP, "DMA probe instrumentation missing")
need("dma_config.backlog = 1" in CPP and "dma_config.dma_burst_size = 32" in CPP, "bounded DMA config missing")
need("target, 0, 0, this->get_width(), this->get_height()" in CPP, "full-frame native present missing")
need("FRAME_CACHE_CAPTURE" in CPP and "FRAME_CACHE_PRESENT" in CPP, "probe instrumentation missing")
need("lv_display_flush_ready(this->disp_)" in CPP, "pending DIRECT flush must be explicitly quiesced")
need("qa_frame_cache_hold_" in HDR, "QA ownership hold state missing")
need("this->set_paused(true, false)" in CPP and "this->set_paused(false, false)" in CPP, "QA must use established LVGL pause/resume path")
loop_body = CPP.split("void LvglComponent::loop() {", 1)[1].split("}", 1)[0]
need("qa_frame_cache_hold_" not in loop_body, "QA must not alter normal LVGL loop path")
need("qa_lvgl_onscreen_frame_" in HDR and "qa_lvgl_draw_frame_" in HDR, "native ownership restore state missing")
need("this->last_presented_frame_ != this->qa_lvgl_onscreen_frame_" in CPP, "release ownership restore missing")
print("PASS: 0.115.215 QA AHB-GDMA cached native framebuffer control contract")
