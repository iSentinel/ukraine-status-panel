#!/usr/bin/env python3
"""Keep Spotify Player transport read-only, fixed, bounded and TLS-only."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/'components/usp_spotify_auth/playback_transport.cpp').read_text()
hpp=(ROOT/'components/usp_spotify_auth/playback_transport.h').read_text()
reg=(ROOT/'scripts/run-regression.sh').read_text()
assert 'https://api.spotify.com/v1/me/player?additional_types=episode' in cpp
assert 'HTTP_METHOD_GET' in cpp and 'HTTP_METHOD_POST' not in cpp
assert 'Authorization' in cpp and 'Bearer ' in cpp
assert 'disable_auto_redirect = true' in cpp and 'max_authorization_retries = -1' in cpp
assert 'skip_cert_common_name_check = false' in cpp and 'esp_crt_bundle_attach' in cpp
assert 'MAX_PLAYBACK_BODY = 32768' in hpp
for forbidden in ('/pause','/next','/previous','/seek','/volume','HTTP_METHOD_POST','HTTP_METHOD_PUT','HTTP_METHOD_DELETE'):
    assert forbidden not in cpp
assert 'client_secret' not in cpp.lower()
assert 'run spotify-player-transport-native.py' in reg
assert 'run spotify-player-transport-static.py' in reg
print('SPOTIFY_PLAYER_TRANSPORT_STATIC=PASS GET_only fixed_url bearer TLS bounded no_control')
