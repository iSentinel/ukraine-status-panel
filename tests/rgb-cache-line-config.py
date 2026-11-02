#!/usr/bin/env python3
"""Verify RGB bounce mode uses the documented ESP32-S3 cache line size."""

import argparse
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def validate_source():
    """Check both distributable configurations without loading secrets."""
    for name in (
        "ukraine-status-panel",
        "ukraine-status-panel-public-first-flash",
    ):
        text = (ROOT / "firmware" / f"{name}.yaml").read_text()
        assert 'CONFIG_ESP32S3_DATA_CACHE_LINE_64B: "y"' in text, name
        assert 'CONFIG_ESP32S3_DATA_CACHE_LINE_32B: "n"' in text, name
        assert 'CONFIG_ESP32S3_DATA_CACHE_32KB: "y"' in text, name
        assert "pclk_frequency: 20MHz" in text, name
        assert "speed: 80MHz" in text, name
    logger.info("RGB_CACHE_SOURCE=PASS line_bytes=64 cache_kib=32")


def validate_generated(path):
    """Reject a built SDK configuration that ignored the requested choices."""
    options = {}
    for line in path.read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            options[key] = value
    expected = {
        "CONFIG_ESP32S3_DATA_CACHE_LINE_64B": "y",
        "CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE": "64",
        "CONFIG_ESP32S3_DATA_CACHE_SIZE": "0x8000",
        "CONFIG_SPIRAM_SPEED": "80",
    }
    for key, value in expected.items():
        assert options.get(key) == value, f"{key}: {options.get(key)}"
    assert options.get("CONFIG_ESP32S3_DATA_CACHE_LINE_32B") != "y"
    assert options.get("CONFIG_LCD_RGB_RESTART_IN_VSYNC") != "y"
    logger.info("RGB_CACHE_GENERATED=PASS line_bytes=64 cache_kib=32")


def main():
    """Provide source checks and an optional post-build evidence check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sdkconfig", type=Path, help="Completed production sdkconfig"
    )
    args = parser.parse_args()
    validate_source()
    if args.sdkconfig:
        validate_generated(args.sdkconfig)


if __name__ == "__main__":
    main()
