#!/usr/bin/env python3
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
ROOT=Path(__file__).resolve().parents[1]
CPP=(ROOT/'components/lvgl/lvgl_esphome.cpp').read_text()
HDR=(ROOT/'components/lvgl/lvgl_esphome.h').read_text()
RGB=(ROOT/'components/mipi_rgb/mipi_rgb.cpp').read_text()
PROD=(ROOT/'firmware/ukraine-status-panel.yaml').read_text()
PUBLIC=(ROOT/'firmware/ukraine-status-panel-public-first-flash.yaml').read_text()
def need(c,m):
    if not c: raise SystemExit('FAIL: '+m)
for text,name in ((PROD,'production'),(PUBLIC,'public')):
    need(CURRENT_FIRMWARE_MARKER in text,f'{name} version')
    need('- id: schedule_home_native_frame_cache' in text,f'{name} cache schedule missing')
    need('- delay: 900ms' in text,f'{name} cache settle delay missing')
    need('- id: show_home_fast' in text,f'{name} fast return script missing')
    need('id(panel_lvgl).present_home_native_frame_cache()' in text,f'{name} fast native present missing')
    need('id(panel_lvgl).refresh_home_native_frame_cache()' in text,f'{name} native cache refresh missing')
    need(text.count('script.execute: show_home_fast') >= 6,f'{name} direct Home returns not centralized')
    home=text[text.index('- id: page_home'):text.index('- id: page_weather')]
    need('script.execute: schedule_home_native_frame_cache' in home,f'{name} Home load does not schedule cache')
need('config.num_fbs = 3;' in RGB,'third native framebuffer not reserved')
need('refresh_home_native_frame_cache' in HDR and 'present_home_native_frame_cache' in HDR,'production C++ API missing')
refresh=CPP.split('bool LvglComponent::refresh_home_native_frame_cache() {',1)[1].split('bool LvglComponent::present_home_native_frame_cache()',1)[0]
present=CPP.split('bool LvglComponent::present_home_native_frame_cache() {',1)[1].split('bool LvglComponent::qa_capture_native_resident_frame()',1)[0]
need('std::memcpy(buf3, this->last_presented_frame_, frame_bytes)' in refresh,'off-path Home snapshot copy missing')
need('std::memcpy' not in present and 'esp_async_memcpy' not in present,'critical fast-present path must be copy-free')
need('present_native_frame(' in present and 'wait_native_frame_complete()' in present,'native present/wait missing')
need('HOME_FAST_FIRST_PAINT' in present and 'HOME_FAST_CACHE_REFRESH' in refresh,'timing instrumentation missing')
need('cache=MISS' in present,'cache miss must fail open to normal LVGL')
print('PASS: 0.115.218 production resident Home fast-first-paint contract')
