# 🇺🇦 Ukraine Status Panel

[Main README](README.md) · [Русский](README_RU.md) · [Українська](README_UA.md)

A local-first touch dashboard for the **Waveshare ESP32-S3-Touch-LCD-7B**. The panel combines Ukraine alerts, weather, household power monitoring, room/environment information, history, clocks and Spotify controls on one 1024×600 LVGL interface.

## Highlights

- Ukraine alert map and active-alert list
- Weather, current conditions and forecast
- Read-only **Shelly Pro EM-50** voltage/power monitoring
- Battery voltage/status UI for the board 1S Li-Po input
- Optional SHT41/SHT45 support without blocking boot when the sensor is absent
- Spotify authorization, playback controls and artwork pipeline
- Local history, outage timeline and room history
- Touch-first LVGL UI with full-screen map/weather modes
- Authenticated private Web UI and Wi-Fi provisioning
- Guarded OTA flow with rollback/release-integrity checks
- ESPHome **2026.10.1** / ESP-IDF **5.5.6** reference toolchain

## Reference hardware

- Waveshare ESP32-S3-Touch-LCD-7B
- ESP32-S3, 16 MB Flash, 8 MB PSRAM
- 7-inch 1024×600 capacitive touch LCD
- Optional 1S 3.7 V Li-Po battery
- Optional SHT4x on the shared I²C expansion path
- Optional passive buzzer
- Optional Shelly Pro EM-50 on the local network

## Development policy

The project uses a fail-closed validation pipeline. Source changes pass static/native tests, LVGL host rendering where relevant, ESPHome configuration/code generation, Xtensa syntax checks, full software/browser regression and release-integrity checks before a production image can be built. Production candidates are built and transmitted at most once. Visible LCD changes are not accepted until they are confirmed on the physical panel.

## Start

```bash
git clone https://github.com/iSentinel/ukraine-status-panel.git
cd ukraine-status-panel
esphome config firmware/ukraine-status-panel-public-first-flash.yaml
```

The public USB provisioning profile contains no `!secret` references, so it does not require `secrets.yaml`. For a separate private build, copy `secrets.example.yaml` to `firmware/secrets.yaml`, replace all placeholders, and keep it untracked. The public Web Installer is still `validation-required`: no factory image is approved for general flashing until exact-version USB first-flash hardware acceptance. Do not reflash a working device to test these instructions. See [Web Installer guidance](web-installer/README.md).

The public repository intentionally excludes deployment secrets, local IP addresses, private checkpoints, UART logs and frozen production artifacts.

See [ROADMAP.md](ROADMAP.md), [CHANGELOG.md](CHANGELOG.md) and [CONTRIBUTING.md](CONTRIBUTING.md).
