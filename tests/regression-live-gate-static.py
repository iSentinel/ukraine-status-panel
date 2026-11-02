#!/usr/bin/env python3
"""Guard the offline-by-default regression/live hardware split."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
RUNNER = (ROOT / "scripts/run-regression.sh").read_text(encoding="utf-8")


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> int:
    require('RUN_LIVE="${USP_RUN_LIVE:-0}"' in RUNNER, "live QA gate missing")
    require("set USP_RUN_LIVE=1 for physical-device QA" in RUNNER, "skip reason missing")
    live_tests = (
        "shared-settings-live-probe.py",
        "web-ui-hardware-probe.py",
        "web-ui-custom-live-probe.py",
        "web-status-diagnostics-live-probe.py",
        "optional-hardware-live-probe.py",
        "r48_ui_page_smoke.py",
        "ui-regression-live-v2.py",
        "semantic-region-status-live-probe.py",
        "region-threat-ui-live-probe.py",
        "palette-live-probe.py",
        "neptun-status-palette-live-probe.py",
    )
    for test in live_tests:
        require(f"run_live {test}" in RUNNER, f"ungated live test: {test}")
    require(
        "run_live_no_settle r48_runtime_stability.py" in RUNNER,
        "runtime stability probe is not live-gated",
    )
    print("REGRESSION_LIVE_GATE_STATIC=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"REGRESSION_LIVE_GATE_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
