#!/usr/bin/env python3
"""Keep Spotify playback controls explicit, bounded and private-only."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_spotify_auth"


def require_fixed_transport() -> None:
    """Allow only the four active-device endpoints and one-shot HTTPS."""
    text = (COMP / "control_transport.cpp").read_text()
    for suffix in ("play", "pause", "next", "previous"):
        assert f'https://api.spotify.com/v1/me/player/{suffix}' in text
    for forbidden in ("device_id", "context_uri", "position_ms", "/seek", "/volume"):
        assert forbidden not in text
    assert text.count("esp_http_client_perform(client)") == 1
    assert "disable_auto_redirect=true" in text
    assert "skip_cert_common_name_check=false" in text
    assert '"Content-Length","0"' in text


def require_owned_admission() -> None:
    """Scope, current playback facts and one coordinator own admission."""
    auth = (COMP / "spotify_auth.cpp").read_text()
    tokens = (COMP / "usp_spotify_tokens.h").read_text()
    model = (COMP / "playback_model.h").read_text()
    coordinator = (COMP / "control_coordinator.h").read_text()
    block = auth[auth.index("bool SpotifyAuthApi::request_play_pause()") :]
    block = block[: block.index("bool SpotifyAuthApi::copy_controls")]
    assert "state_mutex_" not in block
    assert "request_play_pause" in coordinator and "facts_.playing" in coordinator
    assert "has_modify_scope" in tokens
    assert "control_actions_known" in model and "control_allowed_mask" in model
    assert "take_observation" in coordinator and "take_reauth" in coordinator


def require_ui_and_public_safety() -> None:
    """Only explicit buttons mutate; public first-flash has no control callbacks."""
    private = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    public = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
    lcd = (ROOT / "include/usp_spotify_lcd.h").read_text()
    assert private.count("request_control(") == 2
    assert private.count("request_play_pause()") == 1
    assert "request_control(" not in public and "request_play_pause()" not in public
    assert "copy_controls(&controls)" in private
    assert "enabled(w.progress,playback && values.seek)" in lcd
    assert "enabled(w.volume,playback && values.volume)" in lcd
    assert "fresh_playback(state,phase,now)" in lcd
    assert "snapshot.device_active" in lcd and "snapshot.restricted" in lcd
    assert "request_seek(" not in lcd and "request_volume(" not in lcd


def main() -> None:
    """Run source-only safety checks without provider or device I/O."""
    require_fixed_transport()
    require_owned_admission()
    require_ui_and_public_safety()
    print("SPOTIFY_CONTROLS_STATIC=PASS fixed4 one_shot owned_scope facts stage4_value_gated no_public")


if __name__ == "__main__":
    main()
