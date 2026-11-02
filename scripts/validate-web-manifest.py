#!/usr/bin/env python3
"""Validate public metadata or require acceptance of an exact factory image."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from loguru import logger

from public_release_contract import (
    acceptance_errors,
    declaration_errors,
    metadata_errors,
)

ROOT = Path(__file__).resolve().parents[1]


def yaml_version(path: Path) -> str:
    """Read the declared public version without resolving private secrets."""
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("firmware_version:"):
            return stripped.split(":", 1)[1].strip().strip('"')
    raise ValueError("firmware_version not found")


def arguments() -> argparse.Namespace:
    """Describe normal metadata checks and the stricter publication gate."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--require-hardware-validated",
        action="store_true",
        help="Require the hardware-accepted exact image",
    )
    parser.add_argument(
        "--acceptance-preflight",
        action="store_true",
        help="Check declaration before compilation; no publication",
    )
    parser.add_argument(
        "--factory-image",
        type=Path,
        help="Actual staged factory image to hash and compare",
    )
    parser.add_argument(
        "--site-dir",
        type=Path,
        default=ROOT / "web-installer",
        help="Directory containing manifest and release status",
    )
    parser.add_argument(
        "--public-config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml",
        help="Public YAML supplying the expected firmware version",
    )
    return parser.parse_args()


def validate(args: argparse.Namespace) -> list[str]:
    """Keep metadata validation available while refusing unaccepted images."""
    manifest = json.loads((args.site_dir / "manifest.json").read_text())
    status = json.loads((args.site_dir / "release-status.json").read_text())
    if not isinstance(manifest, dict) or not isinstance(status, dict):
        return ["manifest and release status must be objects"]
    errors = metadata_errors(
        manifest, status, yaml_version(args.public_config)
    )
    if args.require_hardware_validated or args.factory_image is not None:
        errors += acceptance_errors(manifest, status, args.factory_image)
    elif args.acceptance_preflight:
        errors += declaration_errors(manifest, status)
    return errors


def main() -> int:
    """Fail closed on malformed files, missing acceptance, or changed bytes."""
    args = arguments()
    try:
        errors = validate(args)
    except (OSError, ValueError, TypeError, KeyError) as error:
        errors = [str(error)]
    for error in errors:
        logger.error("Web manifest: {}", error)
    if errors:
        return 1
    logger.info(
        "Web manifest: PASS exact_image_checked={}",
        args.factory_image is not None,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
