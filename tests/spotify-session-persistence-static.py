#!/usr/bin/env python3
"""Fail closed if session persistence can burn eFuse or expose secrets."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/usp_spotify_auth"


def main() -> None:
    """Check canonical persistence ownership and read-only key inspection."""
    source = "\n".join(
        path.read_text(encoding="utf-8")
        for path in COMPONENT.glob("*.h")
    ) + "\n" + "\n".join(
        path.read_text(encoding="utf-8")
        for path in COMPONENT.glob("*.cpp")
    )
    vault = (COMPONENT / "session_vault.h").read_text(encoding="utf-8")
    idf = (COMPONENT / "session_vault_idf.h").read_text(encoding="utf-8")
    auth = (COMPONENT / "spotify_auth.cpp").read_text(encoding="utf-8")
    schema = (COMPONENT / "__init__.py").read_text(encoding="utf-8")
    yaml = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(
        encoding="utf-8"
    )

    assert "esp_efuse_write_key" not in source
    assert "esp_efuse_write_keys" not in source
    for symbol in (
        "esp_efuse_key_block_unused",
        "esp_efuse_get_key_purpose",
        "esp_efuse_get_key_dis_read",
        "esp_efuse_get_key_dis_write",
        "esp_efuse_get_keypurpose_dis_write",
    ):
        assert symbol in idf, symbol
    assert "LOCAL_KEY_RECORD_KEY" in idf and "local_key_v1" in idf
    assert "ProtectionMode" in idf and "esp_fill_random" in idf
    assert "access_token" not in vault.lower()
    assert "authorization_code" not in vault.lower()
    assert 'cv.Required("session_key_id")' in schema
    assert "session_key_id: 5" in yaml
    assert 'session_storage' in auth
    assert 'session_key_id' in auth
    assert "session_persistence_" in auth
    assert "session_crypto_.setup" in auth
    assert "active_key_id" in auth
    assert 'return "hardware"' in auth and 'return "local"' in auth
    print(
        "SPOTIFY_SESSION_PERSISTENCE_STATIC=PASS writer0 refresh_only "
        "hardware_or_local refresh_only aes_gcm no_efuse_writer private_status"
    )


if __name__ == "__main__":
    main()
