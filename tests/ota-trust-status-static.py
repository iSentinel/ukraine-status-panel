#!/usr/bin/env python3
"""Guard read-only OTA trust diagnostics and downgrade policy exposure."""
from __future__ import annotations

import json
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")
TRUST = json.loads((ROOT / "security/ota-release-trust.json").read_text(encoding="utf-8"))
VALIDATOR = (ROOT / "scripts/validate-private-ota-manifest.py").read_text(encoding="utf-8")
CONTRACT = (ROOT / "scripts/ota_release_contract.py").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    key_id = str(TRUST["key_id"])
    need(f'ota_trust_key_id: "{key_id}"' in PROD, "firmware trust key id mismatch")
    need('ota_update_policy: "ENCRYPTED OTA / MANIFEST CHECK ONLY / WEB OTA OFF"' in PROD,
         "read-only OTA policy string missing")
    for entity in ("web_ota_trust_key", "web_ota_update_policy", "web_ota_device_verifier"):
        need(f"id: {entity}" in PROD, f"missing {entity}")
        need(entity not in PUBLIC, f"public bootstrap leaked {entity}")
    need('ota: false' in PROD, "browser OTA must remain disabled")
    need('"downgrade_default": "deny"' in CONTRACT, "manifest downgrade policy missing")
    need("--current-version" in VALIDATOR, "current-version verifier gate missing")
    need("--allow-downgrade" in VALIDATOR, "recovery downgrade override missing")
    need("downgrade denied by policy" in VALIDATOR, "fail-closed downgrade message missing")
    print(f"OTA_TRUST_STATUS_STATIC=PASS key_id={key_id}")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_TRUST_STATUS_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
