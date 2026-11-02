#!/usr/bin/env bash
# Run the R10 geometry/accessibility audit against readable and embedded JS.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NODE="${NODE_BIN:-$(command -v node || true)}"
if [[ -z "$NODE" ]]; then NODE="$ROOT/../.qa-tools/node/bin/node"; fi
if [[ -z "${PLAYWRIGHT_BROWSERS_PATH:-}" ]] && [[ -d "$ROOT/../.qa-tools/playwright" ]]; then
  export PLAYWRIGHT_BROWSERS_PATH="$ROOT/../.qa-tools/playwright"
fi
cd "$ROOT"
for source in firmware/webui/usp-web-ui.src.js firmware/webui/usp-web-ui.js; do
  "$NODE" tests/web-layout-browser.cjs --script "$source"
done
