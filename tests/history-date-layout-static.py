#!/usr/bin/env python3
"""Guard E-H2 dated history strings against the real generated font width."""
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/.esphome/build/ukraine-status-panel/src/main.cpp"
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
SOURCE = ROOT / "components/usp_history/history_store.cpp"


def glyphs(source: str, size: int) -> dict[int, list[int]]:
    """Read glyph advances from ESPHome 2026.10 generated C++."""
    match = re.search(
        rf"new\(usp_i18n_{size}\) font::Font\((\w+),", source
    )
    assert match, f"Generated font usp_i18n_{size} missing"
    marker = "font::Glyph " + match.group(1) + "[]"
    line = next(item for item in source.splitlines() if marker in item)
    pattern = r"\{(\d+), \([^)]*\), (\d+), (-?\d+), (-?\d+), (\d+), (\d+)\}"
    values = {
        int(row[0]): [int(value) for value in row[1:]]
        for row in re.findall(pattern, line)
    }
    assert values
    return values


def text_width(text: str, values: dict[int, list[int]]) -> int:
    """Measure cursor advance and right-side ink extent."""
    advance = 0
    right = 0
    for char in text:
        assert ord(char) in values, f"Missing glyph {char!r}"
        glyph = values[ord(char)]
        right = max(right, advance + glyph[1] + glyph[3])
        advance += glyph[0]
    return max(right, advance)


def samples(compact: bool) -> list[str]:
    """Cover max group digits, legacy subgroup and all UI languages."""
    duration = "24:00/24:00" if compact else None
    result = []
    for group in ("1.1", "255.2", "255.?"):
        labels = (("Г", "Графік"), ("Г", "График"), ("G", "Schedule"))
        for prefix, label in labels:
            if compact:
                body = duration
            elif label == "Графік":
                body = "24 год 00 хв/24 год 00 хв"
            elif label == "График":
                body = "24 ч 00 мин/24 ч 00 мин"
            else:
                body = "24h00m/24h00m"
            result.append(f"25.10 23:00 {prefix}{group} {label}: {body}")
    return result


def check_wiring() -> None:
    """Keep compact rendering limited to the narrow dedicated history card."""
    yaml = YAML.read_text()
    start = yaml.index("  - id: refresh_history_ui\n")
    end = yaml.index("\n  - id: localize_page_home\n", start)
    block = yaml[start:end]
    assert block.count("event_summary_compact(") == 3
    dashboard = yaml[yaml.index("  - id: refresh_dashboard_history_ui\n") :]
    assert "event_summary_compact(" not in dashboard.split("\n  - id:", 1)[0]
    source = SOURCE.read_text()
    assert '"%02d.%02d %02d:%02d"' in source
    assert '"%02d:%02d"' in source


def main() -> None:
    """Require both 420px compact and 560px detailed rows to remain one-line."""
    check_wiring()
    values = glyphs(MAIN.read_text(), 18)
    compact = [(text_width(value, values), value) for value in samples(True)]
    detail = [(text_width(value, values), value) for value in samples(False)]
    assert max(width for width, _ in compact) <= 420, max(compact)
    assert max(width for width, _ in detail) <= 560, max(detail)
    logger.info(
        "HISTORY_DATE_LAYOUT_STATIC=PASS compact_max={} detail_max={} font18",
        max(width for width, _ in compact),
        max(width for width, _ in detail),
    )


if __name__ == "__main__":
    main()
