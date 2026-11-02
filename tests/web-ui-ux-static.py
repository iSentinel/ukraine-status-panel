#!/usr/bin/env python3
"""Guard the embedded Web UI against high-impact UI/UX regressions."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSS = (ROOT / "firmware/webui/usp-web-ui.css").read_text(encoding="utf-8")
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text(encoding="utf-8")
MASTER = ROOT / "design-system/ukraine-status-panel/MASTER.md"
PAGE = ROOT / "design-system/ukraine-status-panel/pages/web-ui.md"


def need(condition: bool, message: str) -> None:
    """Fail with a focused regression message."""
    if not condition:
        raise AssertionError(message)


def color(token: str) -> str:
    """Read one root hex color token."""
    match = re.search(rf"{re.escape(token)}:\s*(#[0-9a-fA-F]{{6}})", CSS)
    need(match is not None, f"missing color token {token}")
    return match.group(1)


def luminance(value: str) -> float:
    """Return WCAG relative luminance for a six-digit hex color."""
    rgb = [int(value[i : i + 2], 16) / 255 for i in (1, 3, 5)]
    channels = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in rgb]
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]


def contrast(left: str, right: str) -> float:
    """Return WCAG contrast ratio for two hex colors."""
    light, dark = sorted((luminance(left), luminance(right)), reverse=True)
    return (light + 0.05) / (dark + 0.05)


def check_readability() -> None:
    """Keep input hints and the page surface readable at all sizes."""
    need("input::placeholder" in CSS and "opacity: 1" in CSS,
         "placeholder contrast styling missing")
    need("min-height: 100vh" in CSS, "page surface does not fill the viewport")


def main() -> int:
    """Validate the project-adapted UI/UX Pro Max contract."""
    need(MASTER.exists() and PAGE.exists(), "persisted design system is missing")
    page_text = PAGE.read_text(encoding="utf-8")
    need("Offline/local-first" in page_text, "embedded/offline design override missing")
    need("--color-background:" in CSS and "--color-foreground:" in CSS,
         "semantic color tokens missing")
    need("min-height: 44px" in CSS, "44px touch target baseline missing")
    need(re.search(r"\.usp-tab\s*\{[^}]*min-height:\s*44px", CSS) is not None,
         "tab touch target is below 44px")
    need(re.search(r"\.usp-suggestion\s*\{[^}]*min-height:\s*44px", CSS) is not None,
         "suggestion touch target is below 44px")
    need(":focus-visible" in CSS and "outline: 2px solid var(--color-focus)" in CSS,
         "visible keyboard focus contract missing")
    need("touch-action: manipulation" in CSS, "touch interaction optimization missing")
    need("prefers-reduced-motion: reduce" in CSS, "reduced-motion handling missing")
    check_readability()
    need("transition: all" not in CSS, "blanket CSS transitions are forbidden")
    need("transform: scale(.98)" in CSS, "press feedback contract missing")
    need("opacity: .68" in CSS, "disabled controls are too faint")
    need("border-bottom: 2px solid var(--color-info)" in CSS,
         "active tab lacks a restrained selection cue")
    need("overflow-x: hidden" in CSS, "horizontal overflow guard missing")
    need("@media (min-width: 768px)" in CSS and "@media (min-width: 1024px)" in CSS,
         "responsive breakpoint contract missing")
    need("fonts.googleapis.com" not in CSS and "@import url(" not in CSS,
         "runtime Web UI must not depend on external fonts/assets")
    need("🔒" not in JS, "emoji used as a structural Wi-Fi icon")
    need(contrast(color("--color-foreground"), color("--color-background")) >= 4.5,
         "foreground/background contrast is below WCAG AA")
    need(contrast(color("--color-muted-foreground"), color("--color-background")) >= 4.5,
         "muted text/background contrast is below WCAG AA")
    print(
        "WEB_UI_UX_STATIC=PASS touch=44 focus=visible motion=reduced "
        "offline=1 emoji_icons=0 contrast=AA"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_UI_UX_STATIC=FAIL {exc}")
        raise SystemExit(1)
