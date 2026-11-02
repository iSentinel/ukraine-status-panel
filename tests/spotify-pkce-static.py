#!/usr/bin/env python3
"""Guard M7B Stage-2 PKCE security and integration constraints."""
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include/usp_spotify_pkce.h"
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def need(condition: bool, message: str) -> None:
    """Raise a focused assertion for a missing security contract."""
    if not condition:
        raise AssertionError(message)


def main() -> None:
    """Require secure primitives and forbid secret/network creep in Stage 2 core."""
    header = HEADER.read_text()
    source = YAML.read_text()
    lower = (header + "\n" + source).lower()
    for token in (
        "VERIFIER_BYTES = 32", "VERIFIER_CHARS = 43",
        "STATE_BYTES = 16", "STATE_CHARS = 22",
        "SESSION_TTL_MS = 5U * 60U * 1000U", "esp_fill_random",
        "mbedtls_sha256_starts", "mbedtls_sha256_update",
        "mbedtls_sha256_finish", "code_challenge_method=S256",
        "constant_time_equal", "valid_https_redirect", "Grant",
    ):
        need(token in header, f"missing PKCE contract: {token}")
    need("client_secret" not in lower, "Client Secret entered PKCE source")
    need("http://192." not in header, "LAN HTTP redirect entered PKCE source")
    need("preferences" not in header, "PKCE verifier/state persistence added")
    need("esp_http_client" not in header, "Stage-2 core unexpectedly performs network I/O")
    need("include/usp_spotify_pkce.h" in source, "PKCE helper not in target closure")
    logger.info(
        "SPOTIFY_PKCE_STATIC=PASS s256_only=1 rng=esp_fill_random sha256=mbedtls "
        "client_secret=0 persistence=0 network=0 https_redirect=1"
    )


if __name__ == "__main__":
    main()
