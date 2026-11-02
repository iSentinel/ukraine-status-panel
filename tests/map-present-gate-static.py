#!/usr/bin/env python3
"""Guard the shared map presentation handshake without merging cache lifecycles."""
from pathlib import Path
from loguru import logger
ROOT=Path(__file__).resolve().parents[1]
H=(ROOT/'include/usp_map_present_gate.h').read_text()
F=(ROOT/'include/usp_fullscreen_map_cache.h').read_text()
D=(ROOT/'include/usp_dashboard_map_cache.h').read_text()
assert 'class Gate' in H and 'void arm()' in H and 'bool consume(' in H
for text,name in ((F,'fullscreen'),(D,'dashboard')):
    assert '#include "usp_map_present_gate.h"' in text,name
    assert text.count('present_gate_.arm();')==1,name
    assert text.count('present_gate_.consume(')==1,name
    assert 'self->pending_' not in text and 'self->drawing_' not in text,name
    assert text.count('usp_map_present_gate::Gate present_gate_{};')==1,name
assert 'renderer_.cold_pending()' in F and 'cold_run_offset_' in (ROOT/'include/usp_map_cache_renderer.h').read_text()
assert 'DirtyArea' in D and 'invalidate_(image_, dirty)' in D and 'invalidate_(target, dirty)' in D
assert 'lv_obj_set_pos(image_, 63, 12)' in F and 'lv_obj_set_pos(image_, 24, 63)' in D
assert 'FULLSCREEN_CACHE_PRESENT' in F and 'DASHBOARD_CACHE_PRESENT' in D
logger.info('MAP_PRESENT_GATE_STATIC=PASS consumers2 cold_separate dirty_separate geometry_separate logs_separate')
