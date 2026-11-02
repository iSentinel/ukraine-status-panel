# Project-owned ESP32 component

Pinned from ESPHome 2026.10.0. `preferences.cpp` changes only startup error recovery: it does not automatically erase NVS when init/open fails. Explicit preference reset remains destructive by design. See `UPSTREAM.json` and `ESPHOME_LICENSE`.
