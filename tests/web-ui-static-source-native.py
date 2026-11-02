#!/usr/bin/env python3
"""Check constant expansion used by backup allowlist tests."""
from pathlib import Path

from loguru import logger

from web_ui_static_source import expand_string_constants, read_source


def main():
    """Keep field references visible without replacing quoted identifiers."""
    source = 'const GROUP = "Outage Group Code"; use(GROUP); use("GROUP");'
    expanded = expand_string_constants(source)
    assert 'use("Outage Group Code")' in expanded
    assert 'use("GROUP")' in expanded
    assert "use(UNKNOWN)" == expand_string_constants("use(UNKNOWN)")
    secret = 'const SECRET = "Network Password"; safeBackup(){value(SECRET)}'
    assert 'value("Network Password")' in expand_string_constants(secret)
    root = Path(__file__).resolve().parents[1]
    actual = read_source(root / "firmware/webui/usp-web-ui.src.js")
    backup = actual.split("safeBackup() {", 1)[1].split("maintenancePanel(t)", 1)[0]
    assert "Outage Group Code" in backup
    assert "Network Password" not in backup
    logger.info("WEB_UI_STATIC_SOURCE=PASS aliases, quoted text and secret visibility")


if __name__ == "__main__":
    main()
