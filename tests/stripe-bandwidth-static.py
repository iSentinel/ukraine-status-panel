#!/usr/bin/env python3
"""Keep the stripe probe opt-in, bounded and independent of normal LCD output."""
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Protect diagnostic memory limits and unchanged normal navigation."""
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    action = source.split("    - action: usp_qa_stripe_bandwidth", 1)[1]
    action = action.split("    - action:", 1)[0]
    assert "get_native_frame_buffers" in action
    assert "native_line_period_ns()" in action
    for forbidden in ("show_page", "present_native", "reboot", "set_pclk"):
        assert forbidden not in action
    header = (ROOT / "include/usp_stripe_bandwidth_probe.h").read_text()
    assert "40U * 1024U + bytes" in header
    assert "100000U" in header and "heap_caps_free(scratch)" in header
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in header
    assert "static_cast<uint16_t *>(heap_caps_malloc(bytes, caps))" in header
    driver = (ROOT / "components/mipi_rgb/mipi_rgb.cpp").read_text()
    assert "config.num_fbs = 3;" in driver
    assert "constexpr size_t bounce_buffer_lines = 20;" in driver
    logger.info("STRIPE_PROBE_STATIC=PASS read_only opt_in bounded defaults_unchanged")


if __name__ == "__main__":
    main()
