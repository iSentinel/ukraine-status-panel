#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PINNED_ESPHOME_BIN="/opt/esphome-2026.10.1/bin/esphome"
EXPECTED_ESPHOME_VERSION="2026.10.1"
if [[ -z "${ESPHOME_BIN:-}" ]]; then
  if [[ -x "$PINNED_ESPHOME_BIN" ]]; then
    ESPHOME_BIN="$PINNED_ESPHOME_BIN"
  else
    ESPHOME_BIN="$(command -v esphome || true)"
  fi
fi
if [[ -z "$ESPHOME_BIN" || ! -x "$ESPHOME_BIN" ]]; then
  echo "ERROR: esphome executable not found: ${ESPHOME_BIN:-unset}" >&2
  exit 127
fi
actual_version="$("$ESPHOME_BIN" version | awk '{print $2}')"
if [[ "$actual_version" != "$EXPECTED_ESPHOME_VERSION" ]]; then
  echo "ERROR: ESPHome version mismatch: expected $EXPECTED_ESPHOME_VERSION, got $actual_version" >&2
  exit 78
fi
TMP="$(mktemp "$ROOT/firmware/.ci-profile-XXXXXX.yaml")"
LOG="$(mktemp /tmp/usp-ci-config-XXXXXX.log)"
trap 'rm -f "$TMP" "$LOG"' EXIT
for profile in ukraine-status-panel.yaml ukraine-status-panel-production.yaml; do
  python3 - "$ROOT/firmware/$profile" "$TMP" <<'PY'
from pathlib import Path
import re
import sys
source, destination = map(Path, sys.argv[1:3])
text = source.read_text()
values = {
    "wifi_ssid": "CI_ONLY_SSID",
    "wifi_password": "CI_ONLY_PASSWORD_1234",
    "fallback_ap_password": "CI_ONLY_AP_PASSWORD_1234",
    "api_encryption_key": "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=",
    "web_server_password": "CI_ONLY_WEB_PASSWORD_1234",
    "spotify_client_id": "0123456789abcdef0123456789abcdef",
}
names = set(re.findall(r"!secret\s+(\w+)", text))
assert names == values.keys(), f"Unexpected secret references: {names ^ values.keys()}"
for name, value in values.items():
    text = text.replace(f"!secret {name}", f'"{value}"')
assert "!secret" not in text
destination.write_text(text)
PY
  "$ESPHOME_BIN" config "$TMP" >"$LOG" 2>&1 || {
    cat "$LOG" >&2
    exit 1
  }
  printf 'CI_CONFIG=PASS profile=%s fake_credentials_only\n' "$profile"
done
"$ESPHOME_BIN" config "$ROOT/firmware/ukraine-status-panel-public-first-flash.yaml" >"$LOG" 2>&1 || {
  cat "$LOG" >&2
  exit 1
}
echo "CI_CONFIG=PASS public_first_flash"
