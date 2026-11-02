#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-$(git -C "$ROOT" describe --always --dirty)}"
OUT="${2:-$ROOT/releases}"
mkdir -p "$OUT"
if ! git -C "$ROOT" diff --quiet || ! git -C "$ROOT" diff --cached --quiet; then
  echo "ERROR: tracked working tree is dirty; commit changes first." >&2
  exit 1
fi
archive="$OUT/ukraine-status-panel-${VERSION}-source.tar.gz"
git -C "$ROOT" archive --format=tar.gz --prefix="ukraine-status-panel-${VERSION}/" HEAD > "$archive"
(
  cd "$OUT"
  sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256"
)
if tar -tzf "$archive" | grep -Eq '(^|/)(secrets\.yaml|\.esphome|\.git)(/|$)'; then
  echo "ERROR: forbidden private/build path in source archive" >&2
  exit 1
fi
printf 'Created %s\n' "$archive"
