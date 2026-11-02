#!/usr/bin/env python3
"""Generate the ordinary private profile without engineering Native API actions."""

import argparse
import re
from pathlib import Path

from loguru import logger

HEADER = "# Generated ordinary private profile; edit the shared source, not this file.\n"


def action_names(text):
    """Return custom action names without reading or resolving credentials."""
    return re.findall(r"(?m)^    - action: ([^\n]+)$", text)


def generate(text):
    """Remove exactly the API actions block and preserve all other source bytes."""
    assert text.count("\napi:\n") == 1, "Expected one encrypted Native API"
    api = text.index("\napi:\n") + 1
    tail = text[api + len("api:\n") :]
    next_root = re.search(r"(?m)^[A-Za-z_][A-Za-z0-9_]*:", tail)
    assert next_root is not None
    api_block = tail[: next_root.start()]
    assert not re.search(
        r"(?m)^  services:", api_block
    ), "Legacy services unsupported"
    start = text.index("  actions:\n", api)
    boundary = re.search(
        r"(?m)^(?:\S|  [A-Za-z_][A-Za-z0-9_]*:)",
        text[start + len("  actions:\n") :],
    )
    assert boundary is not None, "API actions have no top-level boundary"
    end = start + len("  actions:\n") + boundary.start()
    actions = text[start:end]
    assert action_names(actions), "Shared engineering actions are missing"
    api_prefix = text[api:start]
    assert "  encryption:\n    key: !secret api_encryption_key" in api_prefix
    result = HEADER + text[:start] + text[end:]
    assert not action_names(result), "Custom Native API actions remain"
    assert (
        "  services:\n" not in api_prefix
    ), "Legacy API services are unsupported"
    return result


def main():
    """Generate explicitly or fail if the checked-in projection is stale."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = generate(args.source.read_text())
    if args.check:
        assert args.output.read_text() == result, "Production profile is stale"
        logger.info("Ordinary private projection: PASS")
        return
    args.output.write_text(result)
    logger.info("Ordinary private profile generated; custom actions removed")


if __name__ == "__main__":
    main()
