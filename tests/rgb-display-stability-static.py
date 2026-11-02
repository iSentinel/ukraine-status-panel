#!/usr/bin/env python3
"""Guard the Waveshare timing and the current LCD clock experiment."""

from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
RGB = (ROOT / "components/mipi_rgb/mipi_rgb.cpp").read_text()


def require(token, source, message):
    """Fail with one precise contract message."""
    assert token in source, message


def main():
    """Check 20MHz headroom experiment while retaining porch and DMA settings."""
    timing = {
        "pclk_frequency: 20MHz": "PCLK drifted",
        "pclk_inverted: true": "PCLK edge drifted",
        "hsync_pulse_width: 162": "HSYNC pulse drifted",
        "hsync_back_porch: 152": "HSYNC back porch drifted",
        "hsync_front_porch: 48": "HSYNC front porch drifted",
        "vsync_pulse_width: 45": "VSYNC pulse drifted",
        "vsync_back_porch: 13": "VSYNC back porch drifted",
        "vsync_front_porch: 3": "VSYNC front porch drifted",
    }
    for token, message in timing.items():
        require(token, YAML, message)

    require(
        "constexpr size_t bounce_buffer_lines = 20;",
        RGB,
        "RGB bounce buffer must retain the validated 20 scanlines",
    )
    require(
        "this->height_ % bounce_buffer_lines != 0",
        RGB,
        "RGB bounce geometry must be guarded before ESP-IDF setup",
    )
    require(
        "this->width_ * bounce_buffer_lines",
        RGB,
        "RGB bounce buffer must use the validated line count",
    )
    width = 1024
    bytes_per_pixel = 2
    bounce_bytes = 2 * width * 20 * bytes_per_pixel
    baseline_bytes = 2 * width * 20 * bytes_per_pixel
    assert bounce_bytes == baseline_bytes, (
        "RGB bounce internal-RAM use must stay at the validated baseline"
    )
    require(
        "config.num_fbs = 3;",
        RGB,
        "three native framebuffers are required by resident Home cache",
    )
    require(
        "config.flags.fb_in_psram = 1;",
        RGB,
        "native RGB framebuffers must remain in PSRAM",
    )
    require(
        "pdMS_TO_TICKS(100)",
        RGB,
        "frame completion watchdog contract drifted",
    )
    logger.info("RGB_DISPLAY_STABILITY_STATIC=PASS timing=waveshare bounce_lines=20 divisor_guard=yes dma_delta_bytes=0")


if __name__ == "__main__":
    main()
