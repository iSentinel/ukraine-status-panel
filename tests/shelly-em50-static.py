#!/usr/bin/env python3
"""Verify the read-only Shelly Pro EM-50 production integration."""

import argparse
from pathlib import Path

from loguru import logger

from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "components/usp_shelly_em/shelly_em_client.cpp"
HEADER = ROOT / "components/usp_shelly_em/shelly_em_client.h"
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
WEB = ROOT / "firmware/webui/usp-web-ui.src.js"
COMPILE = ROOT / "scripts/compile-production.sh"
CI = ROOT / "scripts/ci-config-check.sh"


def require(text: str, values: tuple[str, ...], label: str) -> None:
    """Require every production contract marker in one source."""
    missing = [value for value in values if value not in text]
    if missing:
        raise AssertionError(f"{label} missing: {missing}")


def verify_component() -> None:
    """Check EM1 polling, authentication and no-control policy."""
    cpp = CPP.read_text()
    header = HEADER.read_text()
    require(cpp, ("/rpc/EM1.GetStatus?id=", "HTTP_METHOD_GET",
                  "HTTP_AUTH_TYPE_DIGEST", 'config.username = "admin"',
                  "(!auth && (first || code > 0))"), "component")
    require(header, ("std::array<Em1Channel, 2>", "request_refresh()",
                     "auth_required() const"), "header")
    forbidden = ("Switch.Set", "EM1Data.ResetCounters", "HTTP_METHOD_POST",
                 "/rpc/Switch", "/rpc/EM1.Set")
    found = [value for value in forbidden if value in cpp or value in header]
    if found:
        raise AssertionError(f"write/control RPC present: {found}")


def verify_yaml() -> None:
    """Check configuration, two channels and factual-mains LCD binding."""
    text = YAML.read_text()
    version = canonical_firmware_version()
    require(text, (f'firmware_version: "{version}"', "usp_shelly_em:",
                   'name: "Shelly Enabled"', 'name: "Shelly Host"',
                   'initial_value: "192.0.2.179"',
                   'name: "Shelly Password"',
                   'name: "Shelly Mains Channel"',
                   'name: "Shelly CH0 Voltage"',
                   'name: "Shelly CH1 Voltage"',
                   "refresh_shelly_power_ui", "SHELLY OFFLINE"), "yaml")


def verify_web_and_toolchain() -> None:
    """Check safe Web backup and 2026.10.1 build pins."""
    web = WEB.read_text()
    require(web, ('["Shelly","shelly"]', 'shelly:"Shelly"',
                  'shelly:{enabled:value("Shelly Enabled")',
                  '"Shelly password"'), "web")
    for path in (COMPILE, CI):
        text = path.read_text()
        require(text, ("/opt/esphome-2026.10.1/bin/esphome",
                       'EXPECTED_ESPHOME_VERSION="2026.10.1"'), path.name)


def main() -> int:
    """Run the static production contract checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    verify_component()
    verify_yaml()
    verify_web_and_toolchain()
    logger.info(
        "SHELLY_EM50_STATIC=PASS model=Pro_EM-50 channels=2 "
        "rpc=EM1.GetStatus read_only=1 esphome=2026.10.1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
