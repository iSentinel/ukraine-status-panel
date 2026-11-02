#!/usr/bin/env python3
"""Build or verify the embedded Web UI using the locked Terser dependency.

Prepare tools with npm ci --prefix tools/web-ui. Node.js 20 or newer is
required; NODE_BIN may select its executable. No network is used here.
"""
from __future__ import annotations

import argparse
import gzip
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "firmware/webui/usp-web-ui.src.js"
DEFAULT_OUTPUT = ROOT / "firmware/webui/usp-web-ui.js"
GZIP_BUDGET_BYTES = 16 * 1024


def node_executable() -> str:
    """Find an explicitly selected, installed or project-local Node runtime."""
    selected = os.environ.get("NODE_BIN") or shutil.which("node")
    if selected:
        return selected
    local = ROOT.parent / ".qa-tools/node/bin/node"
    if local.is_file():
        return str(local)
    raise SystemExit("Node.js missing; see tools/web-ui/README.md.")


def gzip_size(source: str) -> int:
    """Return the deterministic gzip size used as the flash budget metric."""
    return len(gzip.compress(source.encode("utf-8"), mtime=0))


def enforce_budget(source: str) -> int:
    """Reject assets whose compressed ESPHome representation is too large."""
    size = gzip_size(source)
    if size > GZIP_BUDGET_BYTES:
        raise SystemExit(
            f"Embedded Web UI gzip size {size} exceeds "
            f"{GZIP_BUDGET_BYTES} byte budget"
        )
    return size


def compact(source: str) -> str:
    """Parse and minify JavaScript without changing external property names."""
    command = [node_executable(), str(ROOT / "tools/web-ui/build.cjs")]
    result = subprocess.run(
        command, input=source, text=True, capture_output=True,
        timeout=45, check=False,
    )
    if result.returncode:
        raise SystemExit(result.stderr.strip() or "Web UI minification failed")
    if not result.stdout.strip():
        raise SystemExit("Web UI minification produced an empty file")
    return result.stdout


def main() -> int:
    """Keep the existing build/check command-line contract."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = compact(args.source.read_text(encoding="utf-8"))
    gzip_bytes = enforce_budget(expected)
    if args.check:
        if not args.output.exists() or args.output.read_text() != expected:
            raise SystemExit("embedded Web UI is stale; run scripts/build-web-ui.py")
        print(
            f"WEB_UI_BUILD=PASS source={args.source.stat().st_size} "
            f"runtime={args.output.stat().st_size} gzip={gzip_bytes} "
            f"budget={GZIP_BUDGET_BYTES}"
        )
        return 0
    args.output.write_text(expected, encoding="utf-8")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
