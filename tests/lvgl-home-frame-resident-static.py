#!/usr/bin/env python3
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
ROOT=Path(__file__).resolve().parents[1]
CPP=(ROOT/'components/lvgl/lvgl_esphome.cpp').read_text()
HDR=(ROOT/'components/lvgl/lvgl_esphome.h').read_text()
RGB=(ROOT/'components/mipi_rgb/mipi_rgb.cpp').read_text()
RGBH=(ROOT/'components/mipi_rgb/mipi_rgb.h').read_text()
PROD=(ROOT/'firmware/ukraine-status-panel.yaml').read_text()
PUBLIC=(ROOT/'firmware/ukraine-status-panel-public-first-flash.yaml').read_text()
def need(c,m):
    if not c: raise SystemExit('FAIL: '+m)
for text,name in ((PROD,'production'),(PUBLIC,'public')):
    need(CURRENT_FIRMWARE_MARKER in text,f'{name} version')
    for a in ('usp_qa_frame_resident_capture','usp_qa_frame_resident_present','usp_qa_frame_resident_release'):
        need((f"- action: {a}" in text) == (name == "production"), f"{name} unexpected QA action policy: {a}")
need('config.num_fbs = 3;' in RGB,'RGB driver must allocate three native framebuffers')
need('void **buf3' in RGBH and 'void **buf3' in RGB,'third framebuffer getter missing')
need('lv_display_set_buffers(' in CPP and 'buf1,\n      buf2,' in CPP,'LVGL must remain bound to fb0/fb1 only')
need('qa_capture_native_resident_frame' in HDR and 'qa_present_native_resident_frame' in HDR,'resident QA API missing')
need('this->qa_native_resident_frame_ = buf3;' in CPP,'resident cache must use fb2')
present=CPP.split('bool LvglComponent::qa_present_native_resident_frame() {',1)[1].split('void LvglComponent::qa_release_native_resident_frame()',1)[0]
need('std::memcpy' not in present and 'esp_async_memcpy' not in present,'resident present path must be copy-free')
need('present_native_frame(' in present and 'wait_native_frame_complete()' in present,'resident native present/wait missing')
need('FRAME_RESIDENT_CAPTURE' in CPP and 'FRAME_RESIDENT_PRESENT' in CPP and 'FRAME_RESIDENT_RELEASED' in CPP,'resident instrumentation missing')
print('PASS: 0.115.215 third-native-framebuffer resident Home cache probe contract')
