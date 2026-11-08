#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FACTORY="${1:?usage: $0 <firmware.factory.bin> [output-dir]}"
OUT="${2:-$ROOT/releases}"
MANIFEST="$ROOT/web-installer/manifest.json"
PUBLIC_YAML="$ROOT/firmware/ukraine-status-panel-public-first-flash.yaml"
version="$(python3 - "$MANIFEST" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding='utf-8'))['version'])
PY
)"
mkdir -p "$OUT"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
bundle="$tmp/ukraine-status-panel-$version-public-first-flash"
mkdir -p "$bundle"
cp "$ROOT/web-installer/index.html" "$bundle/"
cp "$MANIFEST" "$bundle/"
cp "$ROOT/web-installer/release-status.json" "$bundle/"
cp "$ROOT/web-installer/README.md" "$bundle/"
# Keep the same pinned local runtime used by the installer HTML.
cp -a "$ROOT/web-installer/vendor" "$bundle/vendor"
cp "$PUBLIC_YAML" "$bundle/ukraine-status-panel-public-first-flash.yaml"
cp "$FACTORY" "$bundle/firmware.factory.bin"
# Check the copied bytes and metadata that will enter the archive.
python3 "$ROOT/scripts/validate-web-manifest.py" --require-hardware-validated \
  --site-dir "$bundle" --public-config "$bundle/ukraine-status-panel-public-first-flash.yaml" \
  --factory-image "$bundle/firmware.factory.bin"
(
  cd "$bundle"
  sha256sum firmware.factory.bin manifest.json release-status.json index.html \
    ukraine-status-panel-public-first-flash.yaml README.md > SHA256SUMS
  find vendor -type f -print0 | sort -z | xargs -0 sha256sum >> SHA256SUMS
)
archive="$OUT/ukraine-status-panel-$version-public-first-flash.tar.gz"
tar --sort=name --mtime='UTC 2020-01-01' --owner=0 --group=0 --numeric-owner \
  -C "$tmp" -czf "$archive" "$(basename "$bundle")"
(
  cd "$OUT"
  sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256"
)
printf 'Created %s\n' "$archive"
