#!/usr/bin/env python3
"""Require opt-in bounded diagnostics and preserve ordinary page transitions."""
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Reject diagnostic changes to network, Home cache or default animation."""
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    action = source.split("    - action: usp_qa_transition_probe", 1)[1]
    action = action.split("    - action:", 1)[0]
    assert "page != 3 && page != 4" in action
    assert "id(panel_lvgl).show_page(target_page->index," in action
    assert "LV_SCREEN_LOAD_ANIM_MOVE_RIGHT" in action
    assert "LV_SCREEN_LOAD_ANIM_MOVE_LEFT" in action
    assert "set_timeout(0x55535054U, 2000" in action
    assert "present_home_native_frame_cache" not in action
    normal = source.split("  - id: navigate_lcd_page", 1)[1]
    normal = normal.split("  # WEB_RUNTIME_BEGIN: network-profile-script", 1)[0]
    # Alarms plus Spotify add explicit NONE transitions to the ordinary routes.
    assert normal.count("animation: NONE") == 12
    assert "present_home_native_frame_cache()" in normal
    header = (ROOT / "include/usp_transition_probe.h").read_text()
    assert "lv_display_remove_event_cb_with_user_data" in header
    assert "lv_obj_remove_event_cb_with_user_data" in header
    assert "heap_caps_malloc" not in header and "memcpy" not in header
    assert "sample_.complete = sample_.loaded" in header
    logger.info("TRANSITION_STATIC=PASS opt-in bounded no-extra-frame-buffer")


if __name__ == "__main__":
    main()
