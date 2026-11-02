# Contributing

Thank you for helping improve Ukraine Status Panel.

## Reference environment

Use **ESPHome 2026.10.1** and the ESP-IDF version selected by the project configuration. Do not silently upgrade the toolchain in an unrelated change.

## Before submitting a change

1. Keep deployment secrets in `secrets.yaml`; never commit credentials, private addresses or tokens.
2. Run ESPHome configuration validation for the affected private/public profile.
3. Run the relevant static/native/LVGL tests under `tests/`.
4. For UI geometry changes, include a regression that checks bounds/overlap rather than relying only on screenshots.
5. For visible LCD changes, state clearly whether they were physically verified on a Waveshare ESP32-S3-Touch-LCD-7B.
6. Do not add Shelly relay/control calls: the project integration is read-only by design.

## Code style

Prefer small, readable functions and explicit failure handling. Python should follow PEP 8, include useful docstrings, and use `loguru` for project logging. Tests and command-line tools should have deterministic exit codes and useful `--help` output.

## Pull requests

Describe the problem, the intended behavior, validation performed, and any hardware that was unavailable. Do not report a host-render or emulator result as physical LCD acceptance.
