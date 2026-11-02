#!/usr/bin/env python3
"""Guard timeout-only RGB DMA/FIFO resynchronization."""

from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "components/mipi_rgb/mipi_rgb.h").read_text()
RGB = (ROOT / "components/mipi_rgb/mipi_rgb.cpp").read_text()
LVGL = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()


def main():
    """Require recovery only after a confirmed frame-complete timeout."""
    assert "bool recover_native_frame_sync();" in HEADER
    assert RGB.count("esp_lcd_rgb_panel_restart(this->handle_)") == 1
    assert "CONFIG_LCD_RGB_RESTART_IN_VSYNC" not in RGB
    profile = LVGL.index("LVGL_FLUSH_WAIT_PROFILE")
    timeout_start = LVGL.index("if (!frame_complete)", profile)
    flush_ready = LVGL.index("lv_display_flush_ready", timeout_start)
    timeout = LVGL[timeout_start:flush_ready]
    assert "recover_native_frame_sync()" in timeout
    assert "recovery=%s" in timeout
    assert "recover_native_frame_sync()" not in LVGL[:timeout_start]
    logger.info(
        "RGB_TIMEOUT_RECOVERY_STATIC=PASS "
        "policy=timeout-only restart=selective"
    )


if __name__ == "__main__":
    main()
