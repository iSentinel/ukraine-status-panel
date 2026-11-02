# 🇺🇦 Ukraine Status Panel

[English](README_EN.md) · [Русский](README_RU.md) · [Головна](README.md)

**Ukraine Status Panel** — локальна сенсорна інформаційна панель для **Waveshare ESP32-S3-Touch-LCD-7B**. На одному екрані 1024×600 поєднані карта тривог України, погода, живлення, домашні датчики, історія, годинник і керування Spotify.

## Можливості

- карта України та список активних тривог;
- погода, поточні умови й прогноз;
- **Shelly Pro EM-50** — лише читання напруги/потужності, без керування навантаженням;
- відображення напруги й стану штатної 1S Li-Po батареї;
- опціональний SHT41/SHT45, відсутність датчика не блокує запуск панелі;
- авторизація Spotify, керування відтворенням і обкладинки;
- історія приміщення та відключень;
- LVGL-інтерфейс для сенсорного екрана;
- повноекранні режими карти й погоди;
- захищений Web UI та Wi-Fi provisioning;
- guarded OTA з rollback і перевірками цілісності релізу;
- еталонне середовище ESPHome **2026.10.1** / ESP-IDF **5.5.6**.

## Обладнання

Основна плата: **Waveshare ESP32-S3-Touch-LCD-7B**, ESP32-S3, 16 MB Flash, 8 MB PSRAM, 7" capacitive touch LCD 1024×600.

Додатково підтримуються 1S 3.7 V Li-Po, SHT4x через спільну I²C-шину, пасивний бузер і Shelly Pro EM-50 у локальній мережі.

## Політика перевірки

Проєкт використовує fail-closed процес. До production build проходять static/native тести, LVGL host-render перевірки, ESPHome config/codegen, Xtensa syntax, повний software/browser regression і release-integrity. Production-кандидат збирається та передається на пристрій не більше одного разу. Видимі зміни інтерфейсу не вважаються прийнятими, доки їх не підтверджено на реальному LCD.

## Початок роботи

```bash
git clone https://github.com/iSentinel/ukraine-status-panel.git
cd ukraine-status-panel
cp secrets.example.yaml secrets.yaml
esphome config firmware/ukraine-status-panel-public-first-flash.yaml
```

Публічний репозиторій не містить паролів, локальних IP стенду, внутрішніх checkpoint-файлів, UART-логів і frozen production artifacts.

Див. також [ROADMAP.md](ROADMAP.md), [CHANGELOG.md](CHANGELOG.md) та [CONTRIBUTING.md](CONTRIBUTING.md).
