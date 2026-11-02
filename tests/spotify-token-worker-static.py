#!/usr/bin/env python3
"""Enforce the target-specific Spotify worker ownership and memory contract."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/usp_spotify_auth/spotify_auth.cpp"
HEADER = ROOT / "components/usp_spotify_auth/spotify_auth.h"
TRANSPORT = ROOT / "components/usp_spotify_auth/token_transport.cpp"


def main() -> None:
    """Keep the internal-stack task and HTTPS transport fail-closed."""
    source = SOURCE.read_text()
    header = HEADER.read_text()
    transport = TRANSPORT.read_text()
    assert "xTaskCreatePinnedToCoreWithCaps(" in source
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in source
    assert "vTaskDeleteWithCaps(nullptr);" in source
    assert "vTaskDelete(nullptr);" not in source
    assert "RAMUniquePtr<usp_spotify_tokens::Engine>" in header
    assert "https://accounts.spotify.com/api/token" in transport
    assert "disable_auto_redirect=true" in transport
    assert "skip_cert_common_name_check=false" in transport
    assert "HttpsPriority::BACKGROUND" in transport
    print(
        "SPOTIFY_TOKEN_WORKER_STATIC=PASS internal_task psram_data caps_delete fixed_tls background no_redirect"
    )


if __name__ == "__main__":
    main()
