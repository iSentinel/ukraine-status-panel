#!/usr/bin/env python3
"""Prevent the removed provider from returning to active firmware inputs."""
import argparse
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Check both profiles and the actual source policy without device access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    assert not (ROOT / "components/jaam_shadow").exists()
    for name in ("ukraine-status-panel.yaml",
                 "ukraine-status-panel-public-first-flash.yaml"):
        text = (ROOT / "firmware" / name).read_text()
        assert "jaam" not in text.lower(), name
        assert "usp_region:" in text and "region_settings" in text
    policy = (ROOT / "include/usp_alert_source_policy.h").read_text()
    assert "jaam" not in policy.lower()
    assert "Source::NEPTUN" in policy
    assert "Source choose(const Frame &neptun, uint32_t now" in policy
    for path in (ROOT / "components").rglob("*"):
        if path.suffix in (".py", ".cpp", ".h"):
            assert "esp_websocket_client" not in path.read_text(), path
    region = (ROOT / "components/usp_region/region_settings.cpp").read_text()
    assert "make_preference<uint16_t>" in region
    assert "global_preferences->sync()" not in region
    logger.info("NEPTUN_ONLY_STATIC=PASS profiles policy no_provider no_websocket")


if __name__ == "__main__":
    main()
