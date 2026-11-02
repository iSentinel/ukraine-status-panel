#!/usr/bin/env python3
"""Check the provider priority wiring and all ballistic map paths."""
import argparse
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_renderers(text):
    """Require a semantic branch for ballistic and KAB in every legacy region."""
    start = text.index("  - id: alert_render_worker\n")
    end = text.index("\n\n  - id: qa_render_air_alert_low", start)
    worker = text[start:end]
    batch = (ROOT / "include" / "usp_alert_region_batch.h").read_text()
    colors = (ROOT / "include" / "usp_alert_region_colors.h").read_text()
    for state in (64, 128):
        assert f"case {state}:" in batch, state
        assert f"state == {state}" in colors, state
    assert "0xA855F7" in colors and "state == 64" in colors
    assert "usp_alert_region_batch::render(" in worker
    assert worker.count("usp_alert_region_batch::render(") == 5
    for name in ("usp_dashboard_map_cache.h", "usp_fullscreen_map_cache.h"):
        source = (ROOT / "include" / name).read_text()
        assert ("usp_alert_region_colors::color_hex" in source or
                ("0xA855F7" in source and "64" in source)), name


def check_provider(text):
    """Keep independent endpoint validation and visible provider attribution."""
    source = (ROOT / "components/usp_neptun/neptun_client.cpp").read_text()
    apply = text.split("  - id: apply_selected_alert_source\n", 1)[1]
    apply = apply.split("  - id: mark_alert_sources_unavailable\n", 1)[0]
    timestamp = apply.index("id(alert_live_last_update_ms) = frame.received_ms")
    assert timestamp < apply.index("if (changed) {")
    assert source.count("scanner.complete()") == 2
    assert "id(alert_pending_states), 25, id(alert_live_accepted_frames)" in text
    assert "snapshot->threat_entries = threats.entries;" in source
    assert "const uint32_t interval = snap.threat_entries > 0U" in source
    assert "active_region_threats" in source
    assert 'provider=NEPTUN' in text
    assert 'idle_refresh_interval: 20s' in text
    web = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()
    assert 'attribution.href = "https://neptun.in.ua/"' in web
    assert 'neptun_source(id(ui_language))' in text


def main():
    """Run offline checks without changing a physical alert or preference."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    text = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    check_renderers(text)
    check_provider(text)
    logger.info("NEPTUN_PRIMARY_STATIC=PASS ballistic25 cached_maps attribution")


if __name__ == "__main__":
    main()
