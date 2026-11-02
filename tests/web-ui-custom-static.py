#!/usr/bin/env python3
"""Validate the project-local responsive Web UI contract."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
JS_PATH = ROOT / "firmware/webui/usp-web-ui.src.js"
RUNTIME_PATH = ROOT / "firmware/webui/usp-web-ui.js"
CSS_PATH = ROOT / "firmware/webui/usp-web-ui.css"
JS = JS_PATH.read_text()
RUNTIME = RUNTIME_PATH.read_text()
CSS = CSS_PATH.read_text()


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    web = PROD.split("web_server:\n", 1)[1].split("# WEB_RUNTIME_END: web-server", 1)[0]
    required_web = (
        "  version: 3\n",
        "  local: false\n",
        '  css_url: ""\n',
        '  js_url: ""\n',
        "  css_include: webui/usp-web-ui.css\n",
        "  js_include: webui/usp-web-ui.js\n",
        "  include_internal: false\n",
        "  log: false\n",
        "  ota: false\n",
        '    username: "admin"\n',
        "    password: !secret web_server_password\n",
        "    type: digest\n",
    )
    for needle in required_web:
        require(needle in web, f"Web config missing: {needle.strip()}")

    require(CSS_PATH.stat().st_size <= 8192, "custom CSS exceeds 8 KiB source budget")
    remote = re.compile(r"(?:https?:)?//(?:cdn|cdnjs|unpkg|jsdelivr)\b", re.I)
    require(not remote.search(JS + "\n" + RUNTIME + "\n" + CSS), "remote CDN dependency found")
    trusted = 'https://getnubergroupproxy.onrender.com'
    require(JS.count(trusted) == 1, "Kyiv lookup origin missing or duplicated")
    attribution = 'attribution.href = "https://neptun.in.ua/"'
    require(JS.count(attribution) == 1, "NEPTUN attribution anchor missing")
    scrubbed = JS.replace(trusted, "").replace(attribution, "")
    require("http://" not in scrubbed and "https://" not in scrubbed, "unapproved remote JS URL found")
    require("@import" not in CSS.lower(), "CSS @import is not allowed")

    exact = (
        'const TAB_ORDER = ["setup", "overview", "location", "hardware", "svitlo", "network", "shelly", "starlink", "spotify", "maintenance"]',
        '["SvitloBot","svitlo"]',
        '["Network","network"]',
        '["Starlink","starlink"]',
        'const KYIV_GROUP_API = "https://getnubergroupproxy.onrender.com"',
        'new EventSource("/events")',
        "fetch(apiPath(entity, action, params)",
        'entity.mode === 1 ? "password" : "text"',
        "card.append(this.__uspButtonControl(entity, n));",
        'entity.name === "Repeat Setup"',
        "if (grouped.has(entity.sorting_group))",
        'customElements.define("esp-app", UspApp)',
    )
    for needle in exact:
        require(needle in JS, f"custom Web UI contract missing: {needle}")

    forbidden_js = ("innerHTML", "insertAdjacentHTML", "eval(", "new Function(")
    for needle in forbidden_js:
        require(needle not in JS, f"unsafe DOM/JS primitive found: {needle}")

    for token in ("webui/usp-web-ui.css", "webui/usp-web-ui.js", "web_server:\n"):
        require(token not in PUBLIC, f"public bootstrap leaked custom Web runtime: {token}")

    print(
        "WEB_UI_CUSTOM_STATIC=PASS "
        f"source={JS_PATH.stat().st_size} js={RUNTIME_PATH.stat().st_size} css={CSS_PATH.stat().st_size}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_UI_CUSTOM_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
