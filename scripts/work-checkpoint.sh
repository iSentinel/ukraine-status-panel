#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE="$ROOT/.work-state"
mkdir -p "$STATE"

usage() {
  cat <<'EOF'
Usage: work-checkpoint.sh <stage> <message> [status]
       work-checkpoint.sh --from-state

Write actions.log and CURRENT together. --from-state reads current_stage,
current_task and current_task_status from firmware-progress.json.
EOF
}

from_state=0

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "${1:-}" == "--from-state" ]]; then
  from_state=1
  if (( $# != 1 )); then
    usage >&2
    exit 2
  fi
  python_bin="${PYTHON_BIN:-}"
  for candidate in "$ROOT/.qa-venv/bin/python3" "$ROOT/../.qa-venv/bin/python3"; do
    if [[ -z "$python_bin" && -x "$candidate" ]]; then
      python_bin="$candidate"
    fi
  done
  python_bin="${python_bin:-python3}"
  fields=()
  while IFS= read -r -d '' value; do
    fields+=("$value")
  done < <("$python_bin" - "$STATE/firmware-progress.json" <<'PY'
import json
from pathlib import Path
import sys

path = Path(sys.argv[1])
try:
    state = json.loads(path.read_text())
except (OSError, json.JSONDecodeError) as error:
    print(f"Cannot read checkpoint: {error}", file=sys.stderr)
    raise SystemExit(2) from error
keys = ("current_stage", "current_task", "current_task_status")
values = []
for key in keys:
    value = state.get(key)
    if not isinstance(value, str) or not value:
        print(f"Checkpoint field is missing: {key}", file=sys.stderr)
        raise SystemExit(2)
    if any(char in value for char in ("\0", "\r", "\n")):
        print(f"Checkpoint field is not one line: {key}", file=sys.stderr)
        raise SystemExit(2)
    values.append(value)
for value in values:
    sys.stdout.buffer.write(value.encode() + b"\0")
PY
  )
  if (( ${#fields[@]} != 3 )); then
    echo "Could not read all current checkpoint fields." >&2
    exit 2
  fi
  set -- "${fields[0]}" "${fields[1]}" "${fields[2]}"
fi

if (( $# < 2 || $# > 3 )); then
  usage >&2
  exit 2
fi
stage="$1"
message="$2"
status="${3:-IN_PROGRESS}"
for value in "$stage" "$message" "$status"; do
  if [[ "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
    echo "Checkpoint values must be single-line text." >&2
    exit 2
  fi
done
now="$(date --iso-8601=seconds)"
commit="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
printf "%s | %s | %s | commit=%s | %s\n" \
  "$now" "$status" "$stage" "$commit" "$message" >> "$STATE/actions.log"
sync -f "$STATE/actions.log" 2>/dev/null || true
{
  printf "timestamp=%s\nstage=%s\nstatus=%s\ncommit=%s\nmessage=%s\n" \
    "$now" "$stage" "$status" "$commit" "$message"
} > "$STATE/CURRENT.tmp"
sync -f "$STATE/CURRENT.tmp" 2>/dev/null || true
mv -f "$STATE/CURRENT.tmp" "$STATE/CURRENT"

if (( from_state == 1 )) && [[ -f "$STATE/OWNER.json" ]]; then
  "$python_bin" - "$STATE/OWNER.json" "$stage" "$message" "$now" <<'PY'
import json
import os
from pathlib import Path
import sys

path = Path(sys.argv[1])
stage = sys.argv[2]
message = sys.argv[3]
timestamp = sys.argv[4]
owner = json.loads(path.read_text())
if not isinstance(owner, dict):
    raise SystemExit("OWNER.json is not an object")
status = owner.get("status")
if isinstance(status, str) and status.startswith("ACTIVE"):
    owner["current_stage"] = stage
    owner["note"] = message
    owner["recorded_at"] = timestamp
    temp = path.with_name(path.name + ".tmp")
    with temp.open("w") as stream:
        json.dump(owner, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)
PY
fi
