# 🇺🇦 Ukraine Status Panel

[English](README_EN.md) · [Главная](README.md) · [Українська](README_UA.md)

**Ukraine Status Panel** — локальная сенсорная информационная панель для **Waveshare ESP32-S3-Touch-LCD-7B**. На одном экране 1024×600 объединены карта тревог Украины, погода, питание, домашние датчики, история, часы и управление Spotify.

## Возможности

- карта Украины и список активных тревог;
- погода, текущие условия и прогноз;
- **Shelly Pro EM-50** — только чтение напряжения/мощности, без управления нагрузкой;
- отображение напряжения и состояния штатной 1S Li-Po батареи;
- опциональный SHT41/SHT45, отсутствие датчика не мешает загрузке панели;
- авторизация Spotify, управление воспроизведением и обложки;
- история помещения и отключений;
- LVGL-интерфейс, рассчитанный на сенсорный экран;
- полноэкранные режимы карты и погоды;
- защищённый Web UI и Wi-Fi provisioning;
- guarded OTA с rollback и проверками целостности релиза;
- эталонная среда ESPHome **2026.10.1** / ESP-IDF **5.5.6**.

## Оборудование

Основная плата: **Waveshare ESP32-S3-Touch-LCD-7B**, ESP32-S3, 16 MB Flash, 8 MB PSRAM, 7" capacitive touch LCD 1024×600.

Дополнительно поддерживаются 1S 3.7 V Li-Po, SHT4x по общей I²C-шине, пассивный буззер и Shelly Pro EM-50 в локальной сети.

## Подход к тестированию

Проект использует fail-closed процесс. До production build проходят static/native тесты, LVGL host-render проверки, ESPHome config/codegen, Xtensa syntax, полный software/browser regression и release-integrity. Production-кандидат собирается и передаётся на устройство не более одного раза. Изменения интерфейса не считаются принятыми, пока они не проверены на реальном LCD.

## Начало работы

```bash
git clone https://github.com/iSentinel/ukraine-status-panel.git
cd ukraine-status-panel
cp secrets.example.yaml secrets.yaml
esphome config firmware/ukraine-status-panel-public-first-flash.yaml
```

Публичный репозиторий не содержит паролей, локальных IP стенда, внутренних checkpoint-файлов, UART-логов и frozen production artifacts.

См. также [ROADMAP.md](ROADMAP.md), [CHANGELOG.md](CHANGELOG.md) и [CONTRIBUTING.md](CONTRIBUTING.md).
