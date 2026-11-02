#!/usr/bin/env python3
"""Static invariants for read-only playback binding."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
engine = (ROOT / "components/usp_spotify_auth/playback_worker_engine.h").read_text()
auth = (ROOT / "components/usp_spotify_auth/spotify_auth.cpp").read_text()
header = (ROOT / "components/usp_spotify_auth/spotify_auth.h").read_text()
token = (ROOT / "components/usp_spotify_auth/token_worker_engine.h").read_text()
transport = (ROOT / "components/usp_spotify_auth/playback_transport.cpp").read_text()
for contract in (
    "PLAYING_POLL_MS = 5000",
    "PAUSED_POLL_MS = 30000",
    "IDLE_POLL_MS = 60000",
    "state.snapshot.is_playing",
    "BODY_BYTES = 32768",
):
    assert contract in engine
assert "take_reauth()" in engine and "generation()" in engine
assert "force_reauth(" in token and "std::string_view access(uint32_t now) const" in token
assert "playback_context_" in header and "copy_playback" in header
assert "ExternalRAMAllocator<usp_spotify_playback_worker::Worker>" in auth
assert "PlaybackJsonAllocator" in auth and "ALLOC_EXTERNAL" in auth
assert "worker_context_->access(clock.now())" in auth
assert "usp_spotify_controls::poll_playback" in auth
assert "playback_context_->take_reauth()" in auth
assert "ObservedPlaybackTransport" in auth and "OwnedPlaybackBridge" in auth
assert "worker_context_->force_reauth(" in auth
assert "Authorization" in transport and "HTTP_METHOD_GET" in transport
for forbidden in (
    "HTTP_METHOD_PUT", "HTTP_METHOD_DELETE", "/pause", "/next", "/previous",
    "/seek", "/volume",
):
    assert forbidden not in transport
start = auth.index("bool SpotifyAuthApi::copy_playback")
copy_block = auth[start:auth.index("/** Shutdown", start)]
assert "access(" not in copy_block
assert "refresh" not in copy_block and "token" not in copy_block.lower()
print(
    "SPOTIFY_PLAYBACK_WORKER_STATIC=PASS adaptive5_30_60 generation "
    "401_reauth bounded_snapshot token_private GET_only no_control"
)
