#!/usr/bin/env python3
"""Guard the public installer against generic/marketing UI regressions."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTML = (ROOT / "web-installer/index.html").read_text(encoding="utf-8")


def need(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def main() -> int:
    for token in ("--bg-deep: #07111f", "--panel: #0c1828", "--border: #263d57"):
        need(token in HTML, f"missing project surface token: {token}")
    need("grid-template-columns: repeat(3" in HTML, "desktop three-step flow missing")
    need("@media (max-width: 620px)" in HTML, "mobile adaptation missing")
    need("min-height: 44px" in HTML, "44px language target missing")
    need('aria-pressed="false"' in HTML and "setAttribute('aria-pressed'" in HTML,
         "language selection is not exposed accessibly")
    need(":focus-visible" in HTML, "visible keyboard focus missing")
    need("prefers-reduced-motion: reduce" in HTML, "reduced-motion contract missing")
    for forbidden in ("linear-gradient", "backdrop-filter", "box-shadow", "transition: all"):
        need(forbidden not in HTML, f"forbidden decorative style returned: {forbidden}")
    for key in ("stepUsb", "stepInstall", "stepWifi", "releaseGate"):
        need(HTML.count(key) >= 4, f"{key} is not localized in UA/RU/EN")
    need('manifest="manifest.json"' in HTML, "ESP Web Tools manifest binding changed")
    print("WEB_INSTALLER_DESIGN_STATIC=PASS operate=1 steps=3 touch44=1 languages=3 motion=reduced")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_INSTALLER_DESIGN_STATIC=FAIL {exc}")
        raise SystemExit(1)
