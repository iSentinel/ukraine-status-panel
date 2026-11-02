#!/usr/bin/env python3
"""Guard the authenticated minimal local Web UI surface."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "firmware" / "ukraine-status-panel.yaml"
PUBLIC = ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml"
EXAMPLE = ROOT / "secrets.example.yaml"
GENERATOR = ROOT / "scripts" / "generate-public-first-flash.py"


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle!r}")


def slice_section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def main() -> int:
    prod = PRODUCTION.read_text(encoding="utf-8")
    public = PUBLIC.read_text(encoding="utf-8")

    web = slice_section(prod, "web_server:\n", "\n# Shared bus:")
    for needle, label in (
        ("  version: 3\n", "Web Server v3"),
        ("  local: false\n", "custom embedded frontend mode"),
        ('  css_url: ""\n', "no remote CSS URL"),
        ('  js_url: ""\n', "no remote JS URL"),
        ("  css_include: webui/usp-web-ui.css\n", "local CSS include"),
        ("  js_include: webui/usp-web-ui.js\n", "local JS include"),
        ("  include_internal: false\n", "internal entity exclusion"),
        ("  log: false\n", "Web log endpoint disabled"),
        ("  ota: false\n", "Web OTA disabled"),
        ("    username: \"admin\"\n", "fixed login name"),
        ("    password: !secret web_server_password\n", "private password"),
        ("    type: digest\n", "Digest authentication"),
    ):
        require(web, needle, label)
    if "type: basic" in web:
        raise AssertionError("Basic authentication is forbidden")

    controls = slice_section(prod, "select:\n", "\n# RGB565 1024x600 panel.")
    for name in ("UI Language", "Alert Palette", "Outage Group"):
        require(controls, f'name: "{name}"', f"safe control {name}")
    for setter in ("set_ui_language", "set_alert_palette", "set_outage_group"):
        require(controls, f"id: {setter}\n", f"shared setter delegation {setter}")
    for pattern, label in (
        (r"id\(ui_language\)\s*=(?!=)", "direct language write"),
        (r"id\(ui_alert_palette\)\s*=(?!=)", "direct palette write"),
    ):
        if re.search(pattern, controls):
            raise AssertionError(f"unsafe/duplicated generic Web control path: {label}")
    if "id(outage_client).set_group(" in controls:
        raise AssertionError("unsafe/duplicated generic Web outage control path")

    # 0.115.145 intentionally exposes Region and Weather in the authenticated
    # local Web UI. They are safe only when they delegate to the same shared
    # backends used by the touch UI; direct state mutation stays forbidden.
    for needle, label in (
        ("id: web_selected_region_select", "region selector"),
        ("id(set_selected_region).execute", "shared region setter"),
        ("id: web_weather_city_search", "weather city search"),
        ("id(weather_client).request_city_search(x, id(ui_language))", "weather search API"),
    ):
        require(controls, needle, label)
    for needle, label in (
        ("id(weather_client).select_search_result(0)", "weather result API"),
        ("id(weather_client).request_refresh()", "weather refresh API"),
    ):
        require(prod, needle, label)
    if re.search(r"id\(ui_selected_region_id\)\s*=(?!=)", controls):
        raise AssertionError("unsafe direct region-state write in Web controls")

    for entity_id in (
        "qa_render_air_alert_low_button",
        "qa_render_air_alert_button",
        "qa_render_explosion_button",
        "qa_render_semantic_mixed_button",
    ):
        require(
            prod,
            f"    id: {entity_id}\n    internal: true\n",
            f"internal QA entity {entity_id}",
        )

    require(
        EXAMPLE.read_text(encoding="utf-8"),
        'web_server_password: "REPLACE_WITH_RANDOM_WEB_PASSWORD"',
        "documented Web password secret",
    )

    for forbidden in ("web_server:\n", "web_server_password", "!secret"):
        if forbidden in public:
            raise AssertionError(f"public bootstrap leaked production Web config: {forbidden}")

    with tempfile.TemporaryDirectory(prefix="usp-web-public-") as temp_dir:
        generated = Path(temp_dir) / "public.yaml"
        subprocess.run(
            [sys.executable, str(GENERATOR), str(PRODUCTION), str(generated)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        regenerated = generated.read_text(encoding="utf-8")
        if regenerated != public:
            raise AssertionError("checked-in public YAML is not generator-reproducible")
        for forbidden in ("web_server:\n", "web_server_password", "!secret"):
            if forbidden in regenerated:
                raise AssertionError(f"generator leaked private Web config: {forbidden}")

    print("WEB_UI_SAFE_SURFACE_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ValueError) as exc:
        print(f"WEB_UI_SAFE_SURFACE_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
