#!/usr/bin/env python3
"""Describe a public factory candidate without accepting it."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from loguru import logger

from public_release_contract import image_identity

ROOT = Path(__file__).resolve().parents[1]


def describe(image: Path, manifest: Path) -> dict:
    """Record immutable image identity; leave hardware acceptance false."""
    data = json.loads(manifest.read_text())
    size, digest = image_identity(image)
    run_id = os.environ.get("GITHUB_RUN_ID")
    receipt = {
        "profile": "public-first-flash",
        "version": data["version"],
        "factory_sha256": digest,
        "factory_size": size,
        "evidence": "",
        "source_commit": os.environ.get("GITHUB_SHA", ""),
    }
    if run_id:
        if not run_id.isdecimal() or int(run_id) <= 0:
            raise ValueError("Invalid GITHUB_RUN_ID")
        receipt["artifact_run_id"] = int(run_id)
    return {
        "schema": 1,
        "version": data["version"],
        "public_first_flash_hardware_validated": False,
        "hardware_acceptance": receipt,
        "note": "Software candidate; hardware acceptance is not recorded.",
    }


def main() -> None:
    """Write candidate metadata for the exact file retained by CI."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--manifest", type=Path, default=ROOT / "web-installer/manifest.json"
    )
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output exists; preserve the earlier candidate record")
    record = describe(args.image, args.manifest)
    args.output.write_text(json.dumps(record, indent=2) + "\n")
    logger.info("PUBLIC_CANDIDATE_RECORDED hardware_validated=false")


if __name__ == "__main__":
    main()
