# Wi-Fi-only captive portal

Основа: bundled captive_portal из ESPHome2026.9.1.
Глобальная установка не изменена. C++ WiFi/DNS/provisioning sources
скопированы без изменений. AUTO_LOAD больше не добавляет ota.web_server.
Это относится к ordinary, engineering и public USB профилям.

portal.html сохраняет исходный Wi-Fi интерфейс; удалена только форма
POST /update. captive_index.h содержит проверяемые gzip/Brotli версии.
Существующий C++ interface INDEX_GZ/INDEX_BR и обе compression опции сохранены.

    python3 scripts/build-portal-assets.py --check
    python3 tests/captive-portal-no-ota-static.py
    node tests/captive-portal-ui-browser.cjs

Node.js нужен для Brotli codec; сторонние npm пакеты для кодека не нужны.
Генерация assets выполняется без --check. Публичные Wi-Fi/Improv Serial
транспорты сохраняются. Native encrypted OTA обычного частного профиля
остаётся отдельно настроенным. Не добавляйте Web OTA в YAML.

Browser test использует synthetic portal.test и не обращается к устройству.
Физический AP/provisioning на рабочей панели не переключается ради проверки.
