#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

forbidden='(^|/)(secrets\.yaml|\.env|id_rsa|id_ed25519|private[^/]*\.key)$'
private_key='BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY'
api_key='api_encryption_key:[[:space:]]*[A-Za-z0-9+/]{40,}={0,2}'

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  if git ls-files | grep -E "$forbidden"; then
    echo "ERROR: private credential filename is tracked" >&2
    exit 1
  fi
  if git grep -IlE "$private_key" -- . ':!secrets.example.yaml' | grep -q .; then
    echo "ERROR: private-key marker found in tracked files" >&2
    exit 1
  fi
  if git grep -nE "$api_key" -- . ':!secrets.example.yaml'; then
    echo "ERROR: possible Native API key in tracked source" >&2
    exit 1
  fi
else
  if find . -type f \
      \( -name secrets.yaml -o -name .env -o -name id_rsa \
         -o -name id_ed25519 -o -name 'private*.key' \) \
      -print -quit | grep -q .; then
    echo "ERROR: private credential filename in source tree" >&2
    exit 1
  fi
  while IFS= read -r -d '' file; do
    if [[ "$(basename "$file")" == secrets.example.yaml ]]; then
      continue
    fi
    if grep -IqE "$private_key" "$file"; then
      echo "ERROR: private-key marker found in source tree: $file" >&2
      exit 1
    fi
    if grep -IqE "$api_key" "$file"; then
      echo "ERROR: possible Native API key in source tree: $file" >&2
      exit 1
    fi
  done < <(find . -type f -print0)
fi

echo "Tracked secret scan: PASS"
