#!/usr/bin/env python3
"""Require R07 semantic security, action labels and complete tab accessibility."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()


def main() -> None:
    """Reject generic status/action/tab semantics."""
    required = (
        "factoryCredentials",
        "customCredentials",
        "defaultPassword",
        'nav.setAttribute("role", "tablist")',
        'button.setAttribute("role", "tab")',
        'button.setAttribute("aria-selected", active ? "true" : "false")',
        "button.tabIndex = active ? 0 : -1",
        "this.__uspTabKey(event, tab)",
        '"ArrowRight"',
        '"ArrowLeft"',
        '"Home"',
        '"End"',
        'content.setAttribute("role", "tabpanel")',
        'content.setAttribute("aria-labelledby", `usp-tab-${this.__uspPrivate_activeTab}`)',
        'const b = node("button", "", n)',
    )
    for token in required:
        assert token in JS, token
    assert "LIVE|FACTORY|CUSTOM|OK|ON|CONNECTED|COMPLETE" not in JS
    assert "FACTORY|STALE|FALLBACK|INCOMPLETE" in JS
    assert "DEFAULT|ERROR|FAIL|OFFLINE|DISCONNECTED" in JS
    assert '["press","Виконати","Выполнить","Run"]' in JS  # fallback only
    print(
        "R07_WEB_SEMANTICS_STATIC=PASS "
        "security=factory-warn/custom-good/default-bad "
        "actions=specific fallback=run tabs=aria+keyboard"
    )


if __name__ == "__main__":
    main()
