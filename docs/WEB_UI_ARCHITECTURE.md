# Public Web UI architecture — 0.115.407-dev

This describes the published source snapshot, **0.115.407-dev**.
It is not a description of the separately maintained `.415` image.

The ESP32-S3 serves an authenticated Web UI alongside the on-device
1024 by 600 LVGL display. Public UI sources reside in
`firmware/webui/` and the ESPHome configuration in
`firmware/ukraine-status-panel.yaml`.

The Web UI groups alerts, weather, map, power, history, Spotify and
network/device configuration. Missing upstream data must be displayed
as unavailable, rather than inferred or replaced by stale readings.
Shelly integration is read-only and may not operate electrical loads.

For changes, run the public browser, static, native, config and
source-integrity checks. Browser tests do not substitute for physical
LCD review. Public USB first-flash remains `validation-required`.
