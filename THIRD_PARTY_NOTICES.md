# Third-party notices

Ukraine Status Panel is built on ESPHome. Some files under `components/` are modified integration components derived from ESPHome 2026.9.1. The installed ESPHome package declares the MIT license. Copyright in upstream ESPHome code remains with its respective contributors.

The firmware also builds against dependencies selected by ESPHome/ESP-IDF, including LVGL and ESP-IDF. Their licenses and notices remain governed by their respective upstream projects and build distributions.

When updating copied integration components, compare them with the corresponding ESPHome release and preserve upstream licensing/attribution requirements.

## NEPTUN public API

The project uses its own read-only API client; it does not incorporate the
NEPTUN application, SDK, branding assets or map artwork. API access is governed
by [NEPTUN API terms](https://neptun.in.ua/api-terms), checked 2026-10-20,
rather than the MIT license of this project. Keep visible NEPTUN attribution
and https://neptun.in.ua/ near the data. The full map and Web diagnostics show
the provider. This panel is informational; follow official warning signals.
The REST interval is 20 seconds, above the documented 5-second minimum.

JAAM runtime code and its WebSocket dependency were removed in .244.
This does not relicense third-party data, fonts, map artwork or other
materials. Historical audit archives are retained as evidence.

## ESPHome 2026.10 ESP32 component pin

`components/esp32/` is pinned from ESPHome 2026.10.0 under its upstream MIT
license. The project modifies only `preferences.cpp`: startup NVS init/open
failures are reported without automatically erasing stored preferences. Explicit
factory/preference reset keeps the upstream erase behavior. `UPSTREAM.json`
records the before/after source hashes and `ESPHOME_LICENSE` retains the license.
