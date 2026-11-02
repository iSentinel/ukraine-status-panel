#!/usr/bin/env python3
"""Execute the captive portal loader and validate Wi-Fi-only embedded assets."""

import argparse
import ast
import importlib.util
from pathlib import Path
from types import SimpleNamespace

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def actual_loader(path, esp32):
    """Execute the real loader with only the platform flag supplied."""
    tree = ast.parse((path / "__init__.py").read_text())
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == "AUTO_LOAD"
    )
    module = ast.Module(body=[function], type_ignores=[])
    namespace = {"CORE": SimpleNamespace(is_esp32=esp32)}
    exec(
        compile(ast.fix_missing_locations(module), str(path), "exec"),
        namespace,
    )
    return namespace["AUTO_LOAD"]()


def assets_module():
    """Load the real semantic asset checker without contacting a device."""
    path = ROOT / "scripts/build-portal-assets.py"
    spec = importlib.util.spec_from_file_location("portal_assets", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_component(path):
    """Reject auto-loaded upload transport and retain provisioning dependencies."""
    for esp32 in (True, False):
        dependencies = actual_loader(path, esp32)
        assert "ota.web_server" not in dependencies, dependencies
        assert "web_server_base" in dependencies
        assert ("socket" in dependencies) == esp32
    assets = assets_module()
    text = (path / "portal.html").read_text()
    assets.validate_html(text)
    payloads = assets.payloads((path / "captive_index.h").read_text())
    import gzip

    assert gzip.decompress(payloads["INDEX_GZ"]).decode() == text
    node = assets.node_binary(None)
    assert (
        assets.brotli(payloads["INDEX_BR"], node, decompress=True).decode()
        == text
    )


def main():
    """Support a before-fail check of the actual bundled upstream component."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--component", type=Path, default=ROOT / "components/captive_portal"
    )
    args = parser.parse_args()
    check_component(args.component)
    logger.info(
        "CAPTIVE_PORTAL_NO_OTA=PASS actual loader WiFi-only gzip/Brotli"
    )


if __name__ == "__main__":
    main()
