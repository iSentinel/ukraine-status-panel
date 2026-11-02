#!/usr/bin/env python3
"""Generate ESP Web Tools manifest from the public first-flash YAML version."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def version_from_yaml(path: Path) -> str:
    """Read the firmware_version substitution without evaluating ESPHome YAML."""
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("firmware_version:"):
            return stripped.split(":", 1)[1].strip().strip('"')
    raise RuntimeError("firmware_version substitution not found")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("yaml", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = {
        "name": "Ukraine Status Panel",
        "version": version_from_yaml(args.yaml),
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 30,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": "firmware.factory.bin", "offset": 0}],
            }
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
