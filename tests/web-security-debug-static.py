#!/usr/bin/env python3
"""Static contract for runtime Web password/reset and temporary Debug Mode."""

from __future__ import annotations

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
GENERATOR = (ROOT / "scripts/generate-public-first-flash.py").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"WEB_SECURITY_DEBUG_STATIC=FAIL {message}")


require(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
require("level: DEBUG\n  initial_level: INFO" in PROD, "normal runtime must start at INFO with DEBUG compiled")
require('name: "Debug Mode (15 min)"' in PROD, "missing Debug Mode switch")
require("restore_mode: ALWAYS_OFF" in PROD, "Debug Mode must be reboot-safe OFF")
require("- delay: 15min" in PROD, "Debug Mode auto-timeout missing")
require("- logger.set_level: DEBUG" in PROD, "Debug Mode does not enable DEBUG")
require("- logger.set_level: INFO" in PROD, "Debug Mode does not restore INFO")

require("id: web_password_override" in PROD, "missing password override storage")
require("type: std::string" in PROD, "password override is not a string")
require("max_restore_data_length: 64" in PROD, "password restore bound missing")
require("update_interval: 250ms" in PROD, "password override persistence interval must be 250ms")
require('name: "New Web Password"' in PROD, "missing password input")
require("mode: password" in PROD, "password input is not masked")
require("min_length: 8" in PROD and "max_length: 64" in PROD, "password length policy missing")
require("usp_web_password_recovery::make_password()" in PROD, "local random password missing")
require("usp_qa_reset_web_password_136" not in PROD, "remote recovery action remains")
require('id: web_password_reset_ui_button' in PROD, "physical reset button missing")
require("WEB_RUNTIME_BEGIN: localize-password-reset" in PROD and "script.execute: refresh_web_password_reset_button" in PROD, "localized reset-button refresh missing")
require("id: web_password_reset_disarm" in PROD and "delay: 10s" in PROD, "two-step reset timeout missing")
require("set_auth_password" in PROD, "runtime Web auth setter missing")
require('x == "admin"' in PROD, "Web password field must reserve admin for physical recovery")
require('id: web_security_status' in PROD, "missing Web security status entity")
require('name: "Web Security Status"' in PROD, "missing Web security status name")
require('DEFAULT (admin/admin)' in PROD, "default credential warning missing")
require('return id(web_password_override);' in PROD, "local recovery password display missing")
require('component.update: web_security_status' in PROD, "security status is not refreshed after auth mutation")

require('name: "Panel settings"' in PROD and 'name: "Maintenance"' in PROD, "Web sorting groups missing")
require('name: "Panel settings"\n      sorting_weight: -100' in PROD, "Panel settings group must sort before built-in technical groups")
require('name: "Maintenance"\n      sorting_weight: -90' in PROD, "Maintenance group must follow Panel settings before built-in technical groups")
require("sorting_group_id: web_group_settings" in PROD, "settings group unused")
require("sorting_group_id: web_group_maintenance" in PROD, "maintenance group unused")

for token in (
    "web_server_password",
    "web_password_override",
    "web_debug_mode",
    "web_password_reset_ui_button",
    "web_security_status",
    "Web Security Status",
    "New Web Password",
    "DEFAULT (admin/admin)",
    "Debug Mode (15 min)",
    "WEB_RUNTIME_BEGIN",
    "WEB_RUNTIME_END",
):
    require(token not in PUBLIC, f"public profile leaked {token}")
require("!secret" not in PUBLIC, "public profile contains secret reference")
require("strip_marked_blocks" in GENERATOR, "public generator does not strip Web runtime blocks")

# Guard against accidentally making the password input optimistic/readable.
password_block = re.search(
    r'(?ms)^  - platform: template\n    id: web_password_input\n(.*?)(?=^# WEB_RUNTIME_END: password-input)',
    PROD,
)
require(password_block is not None, "password input block not found")
block = password_block.group(1)
require("optimistic: false" in block, "password input would publish plaintext state")
require("restore_value: yes" not in block, "password text entity must not own secret persistence")


WEB_IDF = (ROOT / "components/web_server_idf/web_server_idf.cpp").read_text()
require("case 403:" in WEB_IDF and "403 Forbidden" in WEB_IDF,
        "web_server_idf does not preserve HTTP 403 Forbidden")
print("WEB_SECURITY_DEBUG_STATIC=PASS")
