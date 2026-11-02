#!/usr/bin/env bash
# Run real-browser form checks against readable and embedded JavaScript.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == "--help" ]]; then
  echo "Usage: $0"
  echo "Requires npm ci --prefix tools/web-ui and a Playwright Chromium install."
  echo "NODE_BIN and PLAYWRIGHT_BROWSERS_PATH may select local tools."
  exit 0
fi
NODE="${NODE_BIN:-$(command -v node || true)}"
if [[ -z "$NODE" ]]; then
  NODE="$ROOT/../.qa-tools/node/bin/node"
fi
if [[ -z "${PLAYWRIGHT_BROWSERS_PATH:-}" ]] &&    [[ -d "$ROOT/../.qa-tools/playwright" ]]; then
  export PLAYWRIGHT_BROWSERS_PATH="$ROOT/../.qa-tools/playwright"
fi
cd "$ROOT"
for source in firmware/webui/usp-web-ui.src.js firmware/webui/usp-web-ui.js; do
  "$NODE" tests/web-form-browser.cjs --script "$source"
  "$NODE" tests/web-setup-sse-browser.cjs --script "$source"
  "$NODE" tests/web-address-hit-browser.cjs --script "$source"
  "$NODE" tests/web-address-privacy-browser.cjs --script "$source"
  "$NODE" tests/web-reset-confirm-browser.cjs --script "$source"
  "$NODE" tests/web-backup-roundtrip-browser.cjs --script "$source"
  "$NODE" tests/web-page-language-browser.cjs --script "$source"
  "$NODE" tests/web-storage-browser.cjs --script "$source"
  "$NODE" tests/web-ssid-bidi-browser.cjs --script "$source"
  "$NODE" tests/web-ui-r10-browser.cjs --script "$source"
  "$NODE" tests/web-viewport-browser.cjs --script "$source"
done
