#!/usr/bin/env python3
"""Verify the alert render worker stays compact and batch-driven."""
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def worker_lines() -> list[str]:
    """Extract only the map worker from the canonical configuration."""
    lines = YAML.read_text().splitlines()
    start = next(i for i, line in enumerate(lines) if "id: alert_render_worker" in line)
    indent = len(lines[start]) - len(lines[start].lstrip())
    end = next(
        i
        for i in range(start + 1, len(lines))
        if lines[i].strip()
        and len(lines[i]) - len(lines[i].lstrip()) <= indent
        and lines[i].lstrip().startswith("- id:")
    )
    return lines[start:end]


def main() -> None:
    """Run the documented checks and report failures directly."""
    chunk = worker_lines()
    text = "\n".join(chunk)
    assert len(chunk) < 500, len(chunk)
    assert text.count("usp_alert_region_batch::render(") == 5
    assert text.count("delay: 1ms") == 5
    assert text.count("lvgl.image.update") == 0
    assert text.count("id(alert_region_ua_") == 25
    assert "include/usp_alert_region_batch.h" in YAML.read_text()
    cache = (ROOT / "firmware/include/usp_dashboard_map_cache.h").read_text()
    assert "usp_alert_region_colors::color_hex(state, palette)" in cache
    logger.info(
        "ALERT_REGION_WORKER_STATIC=PASS lines={} batches=5 regions=25 yields=5",
        len(chunk),
    )


if __name__ == "__main__":
    main()
