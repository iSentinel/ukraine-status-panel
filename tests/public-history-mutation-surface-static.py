#!/usr/bin/env python3
"""Keep first-flash public firmware unable to mutate semantic history over LAN."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/ukraine-status-panel.yaml"
PUBLIC = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"


def load_generator():
    """Use the exact public projection code used by release packaging."""
    path = ROOT / "scripts/generate-public-first-flash.py"
    spec = importlib.util.spec_from_file_location("public_generator", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    """Reject API/OTA/action reintroduction in generated or checked-in public YAML."""
    module = load_generator()
    generated = module.generate(SOURCE)
    checked = PUBLIC.read_text()
    assert checked == generated, "checked-in public projection is stale"
    for text in (generated, checked):
        assert not re.search(r"(?m)^api:\s*$", text)
        assert not re.search(r"(?m)^ota:\s*$", text)
        assert not re.search(r"(?m)^\s+- action:\s+usp_", text)
        assert "usp_history_record" not in text
        assert "usp_history_clear" not in text
        assert "usp_history_flush" not in text
        assert "usp_qa_history_mark_dirty" not in text
    logger.info(
        "PUBLIC_HISTORY_MUTATION_SURFACE=PASS api0 ota0 actions0 history_mutators0"
    )


if __name__ == "__main__":
    main()
