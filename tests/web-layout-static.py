#!/usr/bin/env python3
"""Keep the R10 card-containment fix and browser audit wired into regression."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CSS = (ROOT / "firmware/webui/usp-web-ui.css").read_text()
REG = (ROOT / "scripts/run-regression.sh").read_text()
TEST = ROOT / "tests/web-layout-browser.cjs"


def main() -> None:
    assert re.search(r"\.usp-actions\s+button\s*\{[^}]*overflow-wrap\s*:\s*anywhere", CSS)
    assert TEST.exists()
    assert "scripts/test-web-layout.sh" in REG
    script = TEST.read_text()
    for token in (
        "WIDTHS = [320, 375, 390, 768, 1024, 1440]",
        'LANGUAGES = ["English", "Русский", "Українська"]',
        'TABS = ["setup", "overview", "location", "hardware", "svitlo", "network", "shelly", "starlink", "spotify", "maintenance"]',
        "touch target <44px",
        "Semantic contrast",
        "keyboardAudit",
        "card content overflow",
    ):
        assert token in script, token
    print("WEB_LAYOUT_STATIC=PASS widths6 languages3 tabs10 touch44 keyboard contrast card_containment")


if __name__ == "__main__":
    main()
