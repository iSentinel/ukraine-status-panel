#!/usr/bin/env python3
"""Keep LVGL config dumping safe when the RGB display fails during setup."""

from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
LVGL = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()


def main():
    """Reject the divide-by-zero path observed on failed .222 hardware boot."""
    assert "this->buffer_frac_ > 0 ? 100 / this->buffer_frac_ : 0" in LVGL, (
        "LVGL dump_config must tolerate buffer_frac_=0 after display setup failure"
    )
    assert "100 / this->buffer_frac_, this->rotation_" not in LVGL, (
        "unguarded LVGL buffer percentage division returned"
    )
    logger.info("LVGL_DISPLAY_FAILURE_STATIC=PASS buffer_frac_zero=safe")


if __name__ == "__main__":
    main()
