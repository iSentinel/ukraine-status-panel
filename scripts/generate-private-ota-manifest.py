#!/usr/bin/env python3
"""Generate a signed-release compatibility manifest for a private OTA image."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from ota_release_contract import build_manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("trust", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = build_manifest(args.config, args.image, args.trust)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
