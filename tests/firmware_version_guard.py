#!/usr/bin/env python3
"""Read the canonical firmware version for static regression contracts."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware" / "ukraine-status-panel.yaml"


def canonical_firmware_version() -> str:
    """Return the version declared by the production firmware profile."""
    text = FIRMWARE.read_text()
    match = re.search(
        r'^  firmware_version: "([^"]+)"$',
        text,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError("canonical firmware_version not found")
    return match.group(1)


CURRENT_FIRMWARE_MARKER = (
    f'firmware_version: "{canonical_firmware_version()}"'
)
