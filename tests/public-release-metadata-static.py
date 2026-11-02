#!/usr/bin/env python3
"""Keep public publication manual and gated on exact-image acceptance."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def workflows() -> None:
    """Require image checks in both publication workflows and the archive."""
    pages = (ROOT / ".github/workflows/pages.yml").read_text()
    release = (ROOT / ".github/workflows/release.yml").read_text()
    bundle = (ROOT / "scripts/make-public-release.sh").read_text()
    assert "workflow_dispatch:" in pages
    assert "push:" not in pages.split("jobs:", 1)[0]
    for text in (pages, release, bundle):
        assert "--require-hardware-validated" in text
        assert "--factory-image" in text
    for text in (pages, bundle):
        assert "--site-dir" in text and "release-status.json" in text


def metadata() -> None:
    """Distinguish ordinary metadata validity from a publication permission."""
    manifest = json.loads((ROOT / "web-installer/manifest.json").read_text())
    status = json.loads(
        (ROOT / "web-installer/release-status.json").read_text()
    )
    assert status.get("schema") == 1
    assert status.get("version") == manifest.get("version")
    assert type(status.get("public_first_flash_hardware_validated")) is bool
    command = [sys.executable, str(ROOT / "scripts/validate-web-manifest.py")]
    normal = subprocess.run(command, capture_output=True, text=True)
    assert normal.returncode == 0, normal.stderr
    strict = subprocess.run(
        command + ["--require-hardware-validated"],
        capture_output=True,
        text=True,
    )
    assert strict.returncode != 0, "Publication without an image was accepted"
    logger.info(
        "PUBLIC_RELEASE_METADATA_STATIC=PASS version={} exact_image=1",
        manifest["version"],
    )


def main() -> None:
    """Check metadata without creating images or hardware declarations."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    workflows()
    metadata()


if __name__ == "__main__":
    main()
