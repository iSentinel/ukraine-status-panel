#!/usr/bin/env python3
"""Validate natural Neptun UART evidence without calling diagnostic actions."""

import argparse
import json
import re
from pathlib import Path

from loguru import logger

KINDS = (
    "uav",
    "fpv",
    "recon",
    "missile",
    "ballistic",
    "kab",
    "mig31k",
    "unknown",
)


def inspect(path, version):
    """Check naturally logged global counts and selected-region restoration."""
    raw = path.read_bytes()
    text = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode(errors="replace"))
    marker = f"Project ukraine-status-panel.panel version {version}"
    assert text.count(marker) == 1
    boot = text[text.index("Log initialized") :]
    regions = re.findall(r"Selected region restored id=(\d+)", boot)
    assert regions, "No natural selected-region restore evidence"
    frames = []
    scheduled = False
    for line in text[text.index(marker) :].splitlines():
        if "NEPTUN request scheduled ok=YES" in line:
            scheduled = True
        if "NEPTUN valid=" not in line:
            continue
        if not scheduled:
            continue
        scheduled = False
        if "NEPTUN valid=YES stale=NO fetch_ok=YES" not in line:
            continue
        fields = dict(re.findall(r"(\w+)=(\S+)", line))
        assert sum(int(fields[k]) for k in KINDS) == int(fields["total"])
        assert int(fields["seq"]) > 0
        frames.append(fields)
    assert frames, "No natural valid NEPTUN frame"
    assert "NEPTUN_ALERTS valid=YES stale=NO fetch_ok=YES" in boot
    return dict(
        status="PASS",
        source="NEPTUN",
        global_counts=frames[-1],
        natural_frames=len(frames),
        selected_region=int(regions[-1]),
        diagnostic_actions_called=False,
        per_region_dump_performed=False,
    )


def main():
    """Save bounded evidence from the already active UART capture."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uart-log", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = inspect(args.uart_log, args.version)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    logger.info(
        "NATURAL_NEPTUN_UART=PASS frames={} region={}",
        result["natural_frames"],
        result["selected_region"],
    )


if __name__ == "__main__":
    main()
