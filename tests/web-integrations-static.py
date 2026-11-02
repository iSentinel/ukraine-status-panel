#!/usr/bin/env python3
"""Validate current Web integrations and public-bootstrap isolation."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    require(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        "usp_svitlobot:", "usp_starlink:", 'name: "SvitloBot Enabled"',
        'name: "Starlink Enabled"', 'name: "Apply Network Settings"',
        'name: "Starlink Probe Now"', 'initial_value: "192.168.100.1"',
        "save_wifi_sta(ssid, password)", "wifi->restart_adapter()",
        "id: apply_network_ip_profile", "id(starlink_client).request_probe()",
        'name: "Starlink GPS Inhibit"', 'name: "Starlink GPS Satellites"',
        'name: "Starlink GPS Status"', "request_gps_inhibit(true)",
        "WEB_RUNTIME_BEGIN: integrations-boot",
        "WEB_RUNTIME_BEGIN: integration-component-list",
        "WEB_RUNTIME_BEGIN: integration-components-config",
    ):
        require(token in PROD, f"production integration contract missing: {token}")
    require("wifi->disable();" not in PROD, "network apply must not hard-disable Wi-Fi")
    boot = PROD.split("WEB_RUNTIME_BEGIN: integrations-boot", 1)[1].split("WEB_RUNTIME_END: integrations-boot", 1)[0]
    require("apply_network_profile" not in boot, "boot path must not reapply credentials/restart Wi-Fi")
    require("apply_network_ip_profile" in boot, "static-IP boot restore path missing")

    for token in (
        'const TAB_ORDER = ["setup", "overview", "location", "hardware", "svitlo", "network", "shelly", "starlink", "spotify", "maintenance"]',
        'const KYIV_GROUP_API = "https://getnubergroupproxy.onrender.com"',
        "usp-active-tab", "Outage Group Code", "TAB_INTRO", "allowEmpty",
        "Starlink GPS Status",
    ):
        require(token in JS, f"Web integration UI contract missing: {token}")
    for token in (
        "usp_svitlobot", "usp_starlink", "web_svitlobot", "web_starlink",
        "web_network_", "SvitloBot", "Starlink", "Network Static IP",
    ):
        require(token not in PUBLIC, f"public bootstrap leaked integration runtime: {token}")
    web_idf = ROOT / "components/web_server_idf/web_server_idf.h"
    require(web_idf.exists(), "local web_server_idf override missing")
    web_idf_source = web_idf.read_text()
    require(
        "std::vector<DeferredEvent, ExternalRAMAllocator<DeferredEvent>>" in web_idf_source,
        "Web SSE deferred queue is not PSRAM-backed",
    )
    require("- web_server_idf" in PROD, "web_server_idf local override not enabled")

    component = ROOT / "components/usp_starlink/starlink_client.cpp"
    require(component.exists(), "Starlink component missing")
    source = component.read_text()
    for token in ("dish_get_status", "dish_set_gps_inhibit", "request_gps_inhibit",
                  "gps_status_valid", "192.168.100.1", "9200"):
        require(token in source, f"Starlink gRPC contract missing: {token}")
    grpc = (ROOT / "components/usp_starlink/starlink_grpc.cpp").read_text()
    for token in ("dish_get_status", "dish_set_gps_inhibit", "parse_gps_status",
                  "nghttp2_submit_request"):
        require(token in grpc, f"Starlink gRPC transport missing: {token}")
    print("WEB_INTEGRATIONS_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"WEB_INTEGRATIONS_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
