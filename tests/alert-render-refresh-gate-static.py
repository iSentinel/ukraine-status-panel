#!/usr/bin/env python3
"""Guard D-ARCH-01 refresh ownership and fail-safe duration."""

import argparse
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def worker_block(source: str) -> str:
    """Return the alert worker only, excluding later QA scripts."""
    start = source.index("  - id: alert_render_worker\n")
    end = source.index("\n\n  - id: qa_render_air_alert_low\n", start)
    return source[start:end]


def main() -> None:
    """Require map-only bounded gate and ownership-based release."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = YAML.read_text()
    worker = worker_block(source)
    assert "id: alert_render_refresh_gate_active" in source
    assert "initial_value: 'false'" in source
    assert "id(lcd_navigation_page) != 1U" in worker
    assert "lv_scr_act() != id(page_map).obj" in worker
    assert "lv_timer_set_period(refresh_timer, 2000UL);" in worker
    assert "5UL * 60UL * 1000UL" not in worker
    assert worker.count("id(alert_render_refresh_gate_active) = true;") == 1
    assert worker.count("id(alert_render_refresh_gate_active) = false;") == 2
    assert "if (!id(alert_render_refresh_gate_active)) return;" in worker
    assert "lv_timer_set_period(refresh_timer, 16UL);" in worker
    assert "lv_timer_ready(refresh_timer);" in worker
    logger.info(
        "ALERT_RENDER_REFRESH_GATE=PASS visible_page_map_only=1 "
        "max_hold_ms=2000 ownership_restore=1 hidden_pages_ungated=1"
    )


if __name__ == "__main__":
    main()
