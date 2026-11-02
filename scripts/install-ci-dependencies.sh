#!/usr/bin/env bash
# Install only the recorded CI packages; do not alter the firmware or panel.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "${1:-}" in
  "") ;;
  -h|--help)
    echo "Usage: $0"
    echo "Install hashed Linux/CPython 3.12 CI dependencies in the active environment."
    echo "Use a dedicated virtual environment; see docs/CI_DEPENDENCIES.md."
    exit 0 ;;
  *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo "Too many arguments." >&2
  exit 2
fi
python -m pip install --require-hashes -r "$ROOT/requirements-ci-bootstrap.lock"
# The pinned bootstrap supplies the source-build tools as well.
python -m pip install --no-build-isolation --require-hashes \
  -r "$ROOT/requirements-ci.lock"
python -m pip check
