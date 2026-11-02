# Spotify playback controls

Controls are explicit user taps only. The firmware exposes fixed active-device
Play/Pause/Next/Previous endpoints, validates the actual granted modify scope and
fresh Player `actions`, shares Player 429 cooldown, and never automatically
replays a command. Seek, volume and device transfer remain disabled.

`spotify-token-worker-native.py` includes the actual single-worker controls
binding. `spotify-lcd-helper-native.py` checks per-action button admission and
change-only LVGL updates. `spotify-controls-static.py` keeps the fixed endpoint,
ownership and public-profile contracts closed.
