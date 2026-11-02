#!/usr/bin/env python3
"""Guard truthful persistent-header time/date rendering before SNTP."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def block(text: str, start_marker: str, end_marker: str) -> str:
    """Return one exact header update block."""
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    return text[start:end]


def main() -> None:
    """Require validity guards and explicit unknown-time placeholders."""
    text = YAML.read_text(encoding="utf-8")
    clock = block(text, "          id: header_clock_label\n", "\n      - if:\n")
    date = block(text, "                id: header_date_label\n", "\n      - if:\n")

    assert "const auto now = id(panel_time).now();" in clock
    assert "if (!now.is_valid()) return std::string(\"--:--:--\");" in clock
    assert "now().hour" not in clock
    assert "now().minute" not in clock
    assert "now().second" not in clock
    assert "char value[16];" in clock

    assert "const auto now = id(panel_time).now();" in date
    assert "if (!now.is_valid()) return std::string(\"--.--.----\");" in date
    assert "now().day_of_month" not in date
    assert "now().month" not in date
    assert "now().year" not in date
    assert "char value[16];" in date
    assert 'text: "--:--:--"' in text
    assert 'text: "--.--.----"' in text
    print("HEADER_TIME_VALIDITY_STATIC=PASS invalid=placeholder raw_invalid_fields=0")


if __name__ == "__main__":
    main()
