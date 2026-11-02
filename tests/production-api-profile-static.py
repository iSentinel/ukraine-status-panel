#!/usr/bin/env python3
"""Validate engineering/ordinary profile separation without compiling firmware."""

import argparse
import importlib.util
import re
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def generator():
    """Import the real profile generator for exact projection tests."""
    path = ROOT / "scripts/generate-private-production.py"
    spec = importlib.util.spec_from_file_location(
        "private_profile_generator", path
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def assert_empty(path):
    """Reject real engineering controls regardless of their name prefix."""
    names = generator().action_names(path.read_text())
    assert not names, f"Custom Native API actions exposed: {len(names)}"


def check_projection():
    """Require a byte-exact ordinary projection and explicit engineering wrapper."""
    source = ROOT / "firmware/ukraine-status-panel.yaml"
    production = ROOT / "firmware/ukraine-status-panel-production.yaml"
    engineering = ROOT / "firmware/ukraine-status-panel-engineering.yaml"
    module = generator()
    assert production.read_text() == module.generate(source.read_text())
    names = module.action_names(source.read_text())
    assert "usp_apply_alert_frame" in names and "usp_history_record" in names
    assert "usp_qa_frame_cache_capture" in names
    wrapper = engineering.read_text()
    assert "!include ukraine-status-panel.yaml" in wrapper
    assert "build/ukraine-status-panel-engineering" in wrapper
    assert_empty(production)
    compiler = (ROOT / "scripts/compile-production.sh").read_text()
    assert "firmware/ukraine-status-panel-production.yaml" in compiler
    assert "generate-private-production.py" in compiler
    try:
        module.generate(
            source.read_text().replace("  actions:\n", "  services:\n", 1)
        )
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError(
            "Generator accepted an unsupported legacy API block"
        )


def check_sibling_options():
    """Retain ordinary API options after actions and reject legacy services."""
    module = generator()
    sample = (
        "substitutions:\n  version: test\napi:\n"
        "  encryption:\n    key: !secret api_encryption_key\n"
        "  actions:\n    - action: usp_apply_alert_frame\n"
        "      then: []\n  max_connections: 3\nsafe_mode:\n"
    )
    projected = module.generate(sample)
    assert "  max_connections: 3" in projected
    assert "usp_apply_alert_frame" not in projected
    legacy = sample.replace("  max_connections: 3", "  services: []")
    try:
        module.generate(legacy)
    except AssertionError:
        pass
    else:
        raise AssertionError("Legacy API services were accepted")


def check_readonly_probe():
    """Reject device action calls in the ordinary hardware checker."""
    import ast

    path = ROOT / "tests/production-readonly-live-probe.py"
    tree = ast.parse(path.read_text())
    allowed = {
        "connect",
        "device_info",
        "list_entities_services",
        "subscribe_states",
        "disconnect",
    }
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
            owner = node.func.value
            if isinstance(owner, ast.Name) and owner.id == "client":
                assert node.func.attr in allowed, node.func.attr


def main():
    """Expose a before-fail check against the previous real build profile."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path)
    args = parser.parse_args()
    if args.profile:
        assert_empty(args.profile)
    else:
        check_projection()
        check_readonly_probe()
        check_sibling_options()
    logger.info(
        "PRODUCTION_API_PROFILE=PASS ordinary0 engineering retained encrypted API"
    )


if __name__ == "__main__":
    main()
