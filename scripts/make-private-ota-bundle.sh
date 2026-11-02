#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:?usage: $0 <private-production firmware.ota.bin> [output-dir]}"
OUT="${2:-$ROOT/releases-private}"
CONFIG="$ROOT/firmware/ukraine-status-panel.yaml"
TRUST="$ROOT/security/ota-release-trust.json"
SIGNING_KEY="${USP_OTA_SIGNING_KEY:-$ROOT/.work-state/ota-signing/release-ed25519-private.pem}"
PYTHON_BIN="${PYTHON_BIN:-$ROOT/../.qa-venv/bin/python3}"
if [[ ! -x "$PYTHON_BIN" ]]; then
  PYTHON_BIN="$(command -v python3)"
fi
if [[ ! -f "$SIGNING_KEY" ]]; then
  echo "ERROR: OTA signing key not found: $SIGNING_KEY" >&2
  exit 2
fi
mkdir -p "$OUT"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
manifest="$tmp/ota-compatibility.json"
signature="$tmp/ota-compatibility.sig.json"
"$PYTHON_BIN" "$ROOT/scripts/generate-private-ota-manifest.py" \
  "$CONFIG" "$IMAGE" "$TRUST" "$manifest"
"$PYTHON_BIN" "$ROOT/scripts/sign-private-ota-manifest.py" \
  "$manifest" "$SIGNING_KEY" "$TRUST" "$signature"
"$PYTHON_BIN" "$ROOT/scripts/validate-private-ota-manifest.py" \
  "$manifest" "$IMAGE" "$signature" "$TRUST"
version="$("$PYTHON_BIN" -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$manifest")"
bundle="$tmp/ukraine-status-panel-$version-private-ota"
mkdir -p "$bundle"
cp "$IMAGE" "$bundle/firmware.ota.bin"
cp "$manifest" "$bundle/ota-compatibility.json"
cp "$signature" "$bundle/ota-compatibility.sig.json"
cat > "$bundle/README.txt" <<'EOF'
PRIVATE SIGNED LOCAL UPDATE BUNDLE — DO NOT PUBLISH OR SHARE.
The OTA image is built from the private production profile and may embed local credentials.
The detached Ed25519 signature authenticates the compatibility manifest, which binds the exact OTA SHA256.
A verifier must use the trust anchor embedded/shipped with trusted firmware/source, never a public key supplied by this bundle.
Browser OTA remains disabled until the signed verifier is integrated into the authenticated update path.
EOF
(
  cd "$bundle"
  sha256sum firmware.ota.bin ota-compatibility.json \
    ota-compatibility.sig.json README.txt > SHA256SUMS
)
archive="$OUT/ukraine-status-panel-$version-private-ota.tar.gz"
tar --sort=name --mtime='UTC 2020-01-01' --owner=0 --group=0 --numeric-owner \
  -C "$tmp" -czf "$archive" "$(basename "$bundle")"
chmod 600 "$archive"
sha256sum "$archive" > "$archive.sha256"
chmod 600 "$archive.sha256"
printf 'Created signed private local bundle: %s\n' "$archive"
