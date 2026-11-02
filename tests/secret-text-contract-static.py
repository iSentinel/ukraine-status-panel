#!/usr/bin/env python3
"""Keep write-only fields and private consumers compatible with prior profiles."""

from pathlib import Path

import yaml
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_fields():
    """Keep stable names and traits so existing secret records can be restored."""
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    config = yaml.load(source, Loader=yaml.BaseLoader)
    fields = {item["id"]: item for item in config["text"]}
    expected = {
        "web_svitlobot_key": ("SvitloBot Channel Key", "true"),
        "web_network_password": ("Network Password", "false"),
    }
    for identifier, (name, restore) in expected.items():
        field = fields[identifier]
        assert field["platform"] == "usp_secret_text"
        assert field["name"] == name and field["mode"] == "password"
        assert field["min_length"] == "0" and field["max_length"] == "64"
        assert field["restore_value"] == restore
        assert "optimistic" not in field
    password = fields["web_password_input"]
    assert password["optimistic"] == "false"
    assert password.get("restore_value", "false") == "false"
    return source


def check_consumers(source):
    """Ensure applying or resetting settings uses the private setter contract."""
    for identifier in ("web_svitlobot_key", "web_network_password"):
        assert f"id({identifier}).state" not in source
        assert f"id({identifier}).secret_value()" in source
    assert "id(web_network_password).clear_value()" in source
    assert "std::initializer_list<esphome::text::Text *>" in source
    public = (
        ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
    ).read_text()
    assert "usp_secret_text" not in public
    assert "web_svitlobot_key" not in public
    assert "web_network_password" not in public


def main():
    """Combine profile contracts with separately executed actual-code tests."""
    check_consumers(check_fields())
    logger.info(
        "SECRET_TEXT_CONTRACT=PASS stable names/traits/private consumers/public isolation"
    )


if __name__ == "__main__":
    main()
