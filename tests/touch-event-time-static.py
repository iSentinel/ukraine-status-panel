#!/usr/bin/env python3
"""Guard physical GT911 event-time wiring against processing-time regressions."""
from pathlib import Path
import re
from loguru import logger
ROOT = Path(__file__).resolve().parents[1]

def main():
    header = (ROOT / "include/usp_lcd_navigation_controller.h").read_text()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert '#include "usp_touch_event_time.h"' in header
    assert "usp_touch_event::EventTime event_time_{};" in header
    for call in ("raw_touch_press(", "raw_touch_point(", "raw_touch_release("):
        assert call in header and f"controller().{call}" in source, call
    assert "touch.state <= 2" not in source
    assert "esphome::touchscreen::STATE_PRESSED" in source
    assert "esphome::touchscreen::STATE_UPDATED" in source
    assert header.count("decision_time_(") == 4
    direct = re.findall(r"gesture_\.(?:press|sample|release)\([^;]*?esphome::millis\(\)", header, flags=re.DOTALL)
    assert not direct, direct
    qa = re.search(r"if \(input == this->qa_input_\).*?now = esphome::millis\(\);.*?return true;", header, flags=re.DOTALL)
    assert qa, "QA input must explicitly retain processing-time semantics"
    assert "self->cancel_gesture_();" in header
    assert "this->cancel_gesture_();" in header
    logger.info("TOUCH_EVENT_TIME_STATIC=PASS physical=raw_event_time qa=processing_time fail_closed=1")

if __name__ == "__main__":
    main()
