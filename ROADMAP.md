# Roadmap

Ukraine Status Panel is under active development for the Waveshare ESP32-S3-Touch-LCD-7B.

## Current focus

- Finish physical validation of the Home header after separating the Weather and Wi-Fi indicators.
- Complete battery UI validation with a 1S 3.7 V Li-Po connected to the board battery input.
- Validate optional SHT4x hardware on a physical SHT41/SHT45 sensor.
- Keep Shelly Pro EM-50 integration read-only and robust through outages and recovery.
- Improve public first-flash and Web Installer documentation for new users.

## Planned

- Passive buzzer alerts with configurable sound profiles.
- Ambient-light based display brightness.
- Additional indoor-air sensors through the shared I²C expansion path.
- More reusable hardware profiles while keeping Waveshare ESP32-S3-Touch-LCD-7B as the reference target.
- Tagged releases with immutable firmware hashes and release notes.

## Release policy

Source changes pass static, native, LVGL, browser and ESP32/Xtensa validation before a production image is built. A production candidate is built and transmitted at most once. Physical UI claims require confirmation on real hardware; host rendering is evidence, not a substitute for the LCD.
