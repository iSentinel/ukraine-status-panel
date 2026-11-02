#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

test -f firmware/ukraine-status-panel.yaml
test -d components
for path in assets include; do
  test -d "$path"
done

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  if git ls-files --cached | grep -E '(^|/)(secrets\.yaml|\.env)$'; then
    echo "ERROR: tracked secrets" >&2
    exit 1
  fi
else
  if find . -type f \( -name secrets.yaml -o -name .env \) -print | grep -q .; then
    echo "ERROR: private credential file in source tree" >&2
    exit 1
  fi
fi

python3 -m compileall -q tests scripts
printf 'Source structure: PASS\n'
