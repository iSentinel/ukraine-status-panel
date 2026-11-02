#!/usr/bin/env python3
"""Check the pinned ESP32 component and NVS preservation wiring."""
import argparse
import hashlib
import json
from pathlib import Path

from loguru import logger

from firmware_version_guard import canonical_firmware_version

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/esp32"
PROFILES = (
    "ukraine-status-panel.yaml",
    "ukraine-status-panel-production.yaml",
    "ukraine-status-panel-public-first-flash.yaml",
)


def function(source, signature):
    """Extract one balanced C++ function for fail-closed token checks."""
    start = source.index(signature)
    position = source.index("{", start)
    depth, end = 1, position + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def verify_cpp():
    """Require preserving startup and retain explicit destructive reset."""
    source = (COMPONENT / "preferences.cpp").read_text()
    opened = function(source, "void ESP32Preferences::open()")
    reset = function(source, "bool ESP32Preferences::reset()")
    assert "nvs_flash_erase" not in opened
    assert "nvs_flash_init" in opened and "nvs_open" in opened
    assert "s_open_err" in opened and "this->nvs_handle = 0" in opened
    assert "nvs_flash_erase" in reset


def verify_pin():
    """Require exact project hash and one declared upstream modification."""
    meta = json.loads((COMPONENT / "UPSTREAM.json").read_text())
    assert meta["upstream"] == "ESPHome" and meta["version"] == "2026.10.0"
    assert meta["modified_files"] == ["preferences.cpp"]
    digest = hashlib.sha256((COMPONENT / "preferences.cpp").read_bytes()).hexdigest()
    assert digest == meta["project_preferences_sha256"]
    assert (COMPONENT / "ESPHOME_LICENSE").is_file()


def verify_upstream_manifest():
    """Protect every unmodified vendored ESP32 source against silent drift."""
    manifest = json.loads((COMPONENT / "UPSTREAM_FILES_SHA256.json").read_text())
    assert manifest
    for name, expected in manifest.items():
        path = COMPONENT / name
        assert path.is_file(), name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        assert actual == expected, name


def verify_profiles():
    """Require all shipped firmware profiles to bind the project component."""
    version = canonical_firmware_version()
    for name in PROFILES:
        text = (ROOT / "firmware" / name).read_text()
        external = text.split("external_components:", 1)[1].split("\n\nesp32:", 1)[0]
        assert "      - esp32\n" in external, name
        assert f'firmware_version: "{version}"' in text, name


def main():
    """Parse standard CLI help and execute static pin checks."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    verify_cpp()
    verify_pin()
    verify_upstream_manifest()
    verify_profiles()
    logger.info("NVS_STARTUP_PRESERVE_STATIC=PASS profiles3 pin2026.10")


if __name__ == "__main__":
    main()
