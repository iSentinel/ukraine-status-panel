#!/usr/bin/env bash
# Generate C++ for source-only CI checks. Never compile or update a device.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "${1:-}" in
  "") ;;
  -h|--help)
    echo "Usage: $0"
    echo "Generate ordinary ESPHome C++ with fixed dummy CI credentials only."
    echo "No firmware image, flash or OTA is produced."
    exit 0 ;;
  *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo "Too many arguments" >&2
  exit 2
fi

ESPHOME_BIN="${ESPHOME_BIN:-$(command -v esphome || true)}"
if [[ -z "$ESPHOME_BIN" || ! -x "$ESPHOME_BIN" ]]; then
  echo "Missing ESPHome executable for source generation" >&2
  exit 127
fi
actual="$("$ESPHOME_BIN" version | awk '{print $2}')"
if [[ "$actual" != "2026.10.1" ]]; then
  echo "Only pinned ESPHome 2026.10.1 is qualified for source-only CI" >&2
  exit 78
fi

tmp="$(mktemp "$ROOT/firmware/.ci-source-XXXXXX.yaml")"
log="$(mktemp /tmp/usp-source-generate-XXXXXX.log)"
trap 'rm -f "$tmp" "$log"' EXIT
python3 - "$ROOT/firmware/ukraine-status-panel.yaml" "$tmp" <<'PY'
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
if names != set(values):
    raise ValueError(f"Unexpected CI secret fields: {names ^ set(values)}")
for name, value in values.items():
    text = text.replace(f"!secret {name}", f'"{value}"')
if "!secret" in text:
    raise ValueError("Unresolved CI secret")
destination.write_text(text)
PY

if ! "$ESPHOME_BIN" compile "$tmp" --only-generate >"$log" 2>&1; then
  tail -70 "$log" >&2
  exit 1
fi
source="$ROOT/firmware/.esphome/build/ukraine-status-panel/src/main.cpp"
if [[ ! -s "$source" ]]; then
  echo "Generated C++ is missing after source-only codegen" >&2
  exit 1
fi
if ! grep -q 'font::Glyph' "$source"; then
  echo "Expected firmware font glyphs are absent from generated C++" >&2
  exit 1
fi
echo "CI_SOURCE_CODEGEN=PASS version=$actual no_image no_device dummy_credentials"
