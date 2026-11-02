#!/usr/bin/env python3
"""Guard the M3 calm-surface design slice without touching render geometry."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CSS = (ROOT / "firmware/webui/usp-web-ui.css").read_text(encoding="utf-8")
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text(encoding="utf-8")
MASTER = (ROOT / "design-system/ukraine-status-panel/MASTER.md").read_text(encoding="utf-8")
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def card_block(card_id: str, next_id: str) -> str:
    start = YAML.index(f"id: {card_id}")
    end = YAML.index(f"id: {next_id}", start + 1)
    return YAML[start:end]


def main() -> int:
    cards = (
        ("dashboard_map_card", "dashboard_alerts_card"),
        ("dashboard_alerts_card", "dashboard_power_card"),
        ("dashboard_power_card", "dashboard_history_card"),
        ("dashboard_history_card", "dashboard_outage_card"),
        ("dashboard_outage_card", "page_map"),
    )
    for card_id, next_id in cards:
        block = card_block(card_id, next_id)
        need("bg_color: 0x0C1828" in block, f"{card_id} surface differs")
        need("border_color: 0x263D57" in block, f"{card_id} border differs")
    need("radial-gradient" not in CSS, "decorative page gradient returned")
    need(re.search(r"body\s*\{[^}]*background:\s*var\(--color-background\)", CSS) is not None,
         "Web page is not on the semantic flat background")
    card = re.search(r"\.usp-card\s*\{([^}]*)\}", CSS)
    need(card is not None, "Web card rule missing")
    need("background: var(--color-card)" in card.group(1), "Web card surface is not semantic")
    need("box-shadow" not in card.group(1), "decorative card shadow returned")
    dot = re.search(r"\.usp-dot\s*\{([^}]*)\}", CSS)
    need(dot is not None and "box-shadow" not in dot.group(1), "status dot glow returned")
    need("const STATUS_LOCAL" in JS and "function statusText" in JS,
         "semantic status presentation map missing")
    for token in ("Актуально", "Застаріло", "Немає даних", "Не підключено", "Помилка",
                  "Устарело", "Нет данных", "Не подключено", "Ошибка"):
        need(token in JS, f"localized status missing: {token}")
    need('entity.name.endsWith("Status")' in JS and 'Alert Live State' in JS
         and 'Alert Map Source' in JS, "status entities are not routed through semantic pills")
    need('return "quiet"' in JS and ".usp-pill.quiet" in CSS,
         "quiet/no-data status role missing")
    need("Operate / monitoring dashboard" in MASTER,
         "authoritative Operate design mode is not documented")
    need("Glassmorphism" in MASTER and "retired" in MASTER,
         "retired generated style is not documented")
    need("transition: all" not in CSS, "blanket transition returned")
    need(".usp-tab.active" in CSS and "border-bottom: 2px solid var(--color-info)" in CSS,
         "receding active-tab cue missing")
    need("opacity: .68" in CSS, "disabled controls became too faint")
    need("transform: scale(.98)" in CSS, "subtle press feedback missing")
    settings = YAML[YAML.index("      - id: page_settings"):YAML.index("      - id: page_diagnostics")]
    need(settings.count("bg_color: 0x0C1828") >= 2,
         "Settings panels diverged from M3 surface")
    need("text_font: usp_i18n_28" in settings, "Settings title is oversized")
    firmware_block = settings[settings.index('text: "${firmware_version}"'):]
    neutral = ("0xFFFFFF", "0xF8FAFC", "0x91A8B2")
    need(any(f"text_color: {color}" in firmware_block[:180] for color in neutral),
         "firmware version must use a neutral text role")
    manual = settings[settings.index('text: "MANUAL"'):]
    need("text_color: 0x91A8B2" in manual[:160],
         "manual policy uses semantic success color")
    print("DESIGN_SYSTEM_M3_STATIC=PASS lcd_cards=5 flat_web=1 semantic_card=1 glow=0")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"DESIGN_SYSTEM_M3_STATIC=FAIL {exc}")
        raise SystemExit(1)
