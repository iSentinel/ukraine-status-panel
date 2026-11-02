#!/usr/bin/env python3
"""Require the embedded JS to be reproducibly built from the readable source."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/webui/usp-web-ui.src.js"
RUNTIME = ROOT / "firmware/webui/usp-web-ui.js"


def main() -> int:
    if not SOURCE.exists():
        raise AssertionError("readable Web UI source missing")
    subprocess.run(
        [sys.executable, str(ROOT / "scripts/build-web-ui.py"), "--check"],
        check=True,
    )
    if SOURCE.stat().st_size <= RUNTIME.stat().st_size:
        raise AssertionError("compact runtime did not reduce source size")
    print(
        f"WEB_UI_BUILD_STATIC=PASS source={SOURCE.stat().st_size} "
        f"runtime={RUNTIME.stat().st_size}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, subprocess.CalledProcessError) as exc:
        print(f"WEB_UI_BUILD_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
