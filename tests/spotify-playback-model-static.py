#!/usr/bin/env python3
"""Keep the read-only Spotify playback model bounded and transport-free."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
header=(ROOT/"components/usp_spotify_auth/playback_model.h").read_text()
auth=(ROOT/"components/usp_spotify_auth/spotify_auth.h").read_text()
reg=(ROOT/"scripts/run-regression.sh").read_text()
assert "sizeof(PlaybackSnapshot) <= 1024" in header
assert "GET" not in header and "api.spotify.com" not in header and "esp_http" not in header
assert "ResponseKind::EMPTY" in header and "status == 204" in header
assert "VOLUME_UNKNOWN = 255" in header and "volume.as<uint8_t>() > 100" in header
assert "https://" in header and "http://" not in header
assert '#include "playback_model.h"' in auth
assert "run spotify-playback-model-native.py" in reg
assert "run spotify-playback-model-static.py" in reg
print("SPOTIFY_PLAYBACK_MODEL_STATIC=PASS bounded transport_free 204 volume https")
