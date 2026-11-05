#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${USP_PRODUCTION_CONFIG:-$ROOT/firmware/ukraine-status-panel-production.yaml}"
PINNED_ESPHOME_BIN="/opt/esphome-2026.10.1/bin/esphome"
EXPECTED_ESPHOME_VERSION="2026.10.1"
if [[ -z "${ESPHOME_BIN:-}" ]]; then
  if [[ -x "$PINNED_ESPHOME_BIN" ]]; then
    ESPHOME_BIN="$PINNED_ESPHOME_BIN"
  else
    ESPHOME_BIN="$(command -v esphome || true)"
  fi
fi
LOCK_FILE="${USP_PRODUCTION_BUILD_LOCK:-$ROOT/.work-state/production-build.lock}"
MODE=compile

usage() {
  cat <<EOF
Usage: $0 [--fingerprint]

Compile the production firmware under an exclusive build-tree lock.

Options:
  --fingerprint  Print the aggregate production input fingerprint and exit.
  -h, --help     Show this help and exit.
EOF
}

case "${1:-}" in
  "") ;;
  --fingerprint) MODE=fingerprint ;;
  -h|--help) usage; exit 0 ;;
  *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo "Too many arguments." >&2
  usage >&2
  exit 2
fi

source_fingerprint() {
  local -a roots=("$CONFIG" "$ROOT/firmware/ukraine-status-panel.yaml" "$ROOT/firmware/ukraine-status-panel-engineering.yaml" "$ROOT/scripts/generate-private-production.py" "$ROOT/scripts/compile-production.sh" "$ROOT/scripts/build-portal-assets.py" "$ROOT/scripts/build-spotify-auth-assets.py" "$ROOT/components" "$ROOT/include" "$ROOT/assets" "$ROOT/firmware/webui" "$ROOT/scripts/build-web-ui.py" "$ROOT/tools/web-ui/build.cjs" "$ROOT/tools/web-ui/package.json" "$ROOT/tools/web-ui/package-lock.json")
  local secrets="$ROOT/firmware/secrets.yaml"
  {
    for path in "${roots[@]}"; do
      if [[ -f "$path" ]]; then
        printf "%s\0" "$path"
      elif [[ -d "$path" ]]; then
        find "$path" \
          -type d -name '__pycache__' -prune -o \
          -type f ! -name '*.pyc' ! -name '*.pyo' -print0
      fi
    done
    if [[ -f "$secrets" ]]; then
      printf "%s\0" "$secrets"
    fi
  } | sort -z | xargs -0 -r sha256sum | sha256sum | cut -d" " -f1
}

mkdir -p "$(dirname "$LOCK_FILE")"
exec 9>"$LOCK_FILE"
if ! flock -n 9; then
  echo "Production build is already running: $LOCK_FILE" >&2
  exit 75
fi

# The ordinary profile must be an exact current projection of the shared source.
PROFILE_PYTHON="${PYTHON_BIN:-python3}"
for candidate in "$ROOT/.qa-venv/bin/python3" "$ROOT/../.qa-venv/bin/python3"; do
  if [[ -x "$candidate" ]]; then
    PROFILE_PYTHON="$candidate"
    break
  fi
done
"$PROFILE_PYTHON" "$ROOT/scripts/generate-private-production.py" \
  "$ROOT/firmware/ukraine-status-panel.yaml" \
  "$ROOT/firmware/ukraine-status-panel-production.yaml" --check

"$PROFILE_PYTHON" "$ROOT/scripts/build-portal-assets.py" --check
"$PROFILE_PYTHON" "$ROOT/scripts/build-spotify-auth-assets.py" --check

python3 "$ROOT/scripts/build-web-ui.py" --check >/dev/null

if [[ ! -f "$CONFIG" ]]; then
  echo "Production config not found: $CONFIG" >&2
  exit 2
fi

if [[ "$MODE" == fingerprint ]]; then
  source_fingerprint
  exit 0
fi

if [[ -z "$ESPHOME_BIN" || ! -x "$ESPHOME_BIN" ]]; then
  echo "ESPHome executable not found: ${ESPHOME_BIN:-unset}" >&2
  exit 2
fi
actual_version="$("$ESPHOME_BIN" version | awk '{print $2}')"
if [[ "$actual_version" != "$EXPECTED_ESPHOME_VERSION" ]]; then
  echo "ESPHome version mismatch: expected $EXPECTED_ESPHOME_VERSION, got $actual_version" >&2
  exit 78
fi

before="$(source_fingerprint)"
echo "Production source fingerprint before build: $before"
"$ESPHOME_BIN" compile "$CONFIG"
after="$(source_fingerprint)"
echo "Production source fingerprint after build:  $after"

if [[ "$before" != "$after" ]]; then
  echo "Production sources changed during build; discard generated artifacts." >&2
  exit 86
fi

echo "Production compile completed under exclusive build lock."
