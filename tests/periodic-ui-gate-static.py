#!/usr/bin/env python3
"""Guard change-driven date and periodic outage UI refresh scheduling."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware" / "ukraine-status-panel.yaml"


def require(text: str, needle: str, label: str) -> None:
    """Fail with a useful message when one scheduling guard disappears."""
    if needle not in text:
        raise RuntimeError(f"missing {label}: {needle!r}")


def main() -> None:
    """Verify the periodic UI gates in the tracked firmware YAML."""
    text = CONFIG.read_text(encoding="utf-8")
    require(text, "static uint32_t last_date_key", "date change gate")
    require(text, "static uint32_t last_minute_key", "minute change gate")
    require(text, "now.day_of_year", "minute key day component")

    legacy = """  - interval: 30s\n    then:\n      - script.execute: refresh_outage_ui"""
    if legacy in text:
        raise RuntimeError("legacy unconditional 30-second outage refresh returned")

    print("PERIODIC_UI_GATE_STATIC=PASS")


if __name__ == "__main__":
    main()
