#!/usr/bin/env python3
"""Shared Native API helpers for Ukraine Status Panel history QA."""

from __future__ import annotations

import base64
from pathlib import Path
import sys

import aioesphomeapi
from loguru import logger
import yaml

REQUIRED_SERVICES = {
    "usp_history_clear",
    "usp_history_dump",
    "usp_history_flush",
    "usp_history_record",
    "usp_qa_history_mark_dirty",
    "usp_stored_shadow_dump",
    "usp_outage_dump",
    "usp_outage_refresh",
    "usp_outage_set_group",
    "usp_qa_set_language",
    "usp_qa_show_history",
    "usp_qa_show_home",
}


def configure_logging() -> None:
    """Use short messages suitable for saved QA logs."""
    logger.remove()
    logger.add(sys.stderr, format="{level}: {message}")


def api_key_expression(config: Path) -> str:
    """Read the api.encryption.key expression from an ESPHome YAML file."""
    lines = config.read_text().splitlines()
    in_api = False
    in_encryption = False
    encryption_indent = 0
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        indent = len(line) - len(line.lstrip())
        if not in_api:
            in_api = line == "api:"
            continue
        if indent == 0 and line != "api:":
            break
        if stripped == "encryption:":
            in_encryption = True
            encryption_indent = indent
            continue
        if in_encryption and indent > encryption_indent:
            if stripped.startswith("key:"):
                return stripped.split(":", 1)[1].strip()
    raise RuntimeError("api.encryption.key not found")


def secret_value(config: Path, secret_name: str) -> str:
    """Read one named value from the ESPHome secrets file."""
    secrets_path = config.parent / "secrets.yaml"
    secrets = yaml.safe_load(secrets_path.read_text()) or {}
    if secret_name not in secrets:
        raise RuntimeError(f"Secret not found: {secret_name}")
    return str(secrets[secret_name]).strip()


def resolve_psk(config: Path) -> str:
    """Resolve and validate the 32-byte Native API Noise key."""
    expression = api_key_expression(config).strip()
    if expression.startswith("!secret "):
        secret_name = expression.split(None, 1)[1].strip()
        value = secret_value(config, secret_name)
    else:
        value = str(yaml.safe_load(expression)).strip()
    decoded = base64.b64decode(value, validate=True)
    if len(decoded) != 32:
        raise RuntimeError("Native API Noise key is not 32 bytes")
    return value


def build_client(
    config: Path,
    address: str,
) -> aioesphomeapi.APIClient:
    """Create a Native API client without opening the connection yet."""
    return aioesphomeapi.APIClient(
        address,
        6053,
        noise_psk=resolve_psk(config),
    )


def expected_version(config: Path) -> str:
    """Read the firmware_version substitution from an ESPHome YAML file."""
    for line in config.read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("firmware_version:"):
            return stripped.split(":", 1)[1].strip().strip('"')
    raise RuntimeError("firmware_version substitution not found")


async def verify_device(
    client: aioesphomeapi.APIClient,
    config: Path | None = None,
) -> None:
    """Confirm the connected panel identity and, optionally, its version."""
    info = await client.device_info()
    name = str(getattr(info, "name", "") or "")
    version = str(getattr(info, "project_version", "") or "")
    build_time = str(getattr(info, "compilation_time", "") or "")
    logger.info("Device name={} version={} build={}", name, version, build_time)
    if config is not None and version != expected_version(config):
        raise RuntimeError(f"Unexpected project version: {version!r}")


async def discover_services(
    client: aioesphomeapi.APIClient,
) -> dict[str, object]:
    """Return Native API services after checking History QA actions."""
    _entities, services = await client.list_entities_services()
    by_name = {str(service.name): service for service in services}
    missing = REQUIRED_SERVICES - set(by_name)
    if missing:
        names = ", ".join(sorted(missing))
        raise RuntimeError(f"Missing history services: {names}")
    logger.info("Required history Native API services discovered")
    return by_name
