#!/usr/bin/env python3
"""Read the accepted public candidate ID without downloading.

Only the repository's publication workflows download the selected artifact.
The actual factory bytes must still pass --require-hardware-validated.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from loguru import logger

from public_release_contract import declaration_errors

ROOT = Path(__file__).resolve().parents[1]


def artifact_inputs(manifest: dict, status: dict) -> dict[str, str]:
    """Permit only a positive numeric run ID and its fixed artifact name."""
    if not isinstance(manifest, dict) or not isinstance(status, dict):
        raise ValueError("Manifest and release status must be objects")
    errors = declaration_errors(manifest, status)
    if errors:
        raise ValueError("; ".join(errors))
    run_id = status["hardware_acceptance"].get("artifact_run_id")
    if type(run_id) is not int or run_id <= 0:
        raise ValueError("hardware receipt needs a positive artifact_run_id")
    return {
        "run_id": str(run_id),
        "artifact_name": f"public-first-flash-{run_id}",
    }


def main() -> None:
    """Output public identifiers without starting builds or publication."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--site-dir", type=Path, default=ROOT / "web-installer"
    )
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.site_dir / "manifest.json").read_text())
    status = json.loads((args.site_dir / "release-status.json").read_text())
    try:
        values = artifact_inputs(manifest, status)
    except (ValueError, TypeError, KeyError) as error:
        logger.error("Public artifact: {}", error)
        raise SystemExit(1) from error
    if args.github_output:
        with args.github_output.open("a") as output:
            output.write(
                "".join(f"{key}={value}\n" for key, value in values.items())
            )
    else:
        print(json.dumps(values, indent=2))


if __name__ == "__main__":
    main()
