# Проверка обложек Spotify

Запуск: python3 tests/spotify-token-worker-native.py --case artwork-binding; python3 tests/spotify-artwork-cache-native.py; python3 tests/spotify-artwork-lcd-native.py [--build-dir новый_каталог]. Нужны Python/loguru, C/C++ compilers, CMake/Ninja, pinned ArduinoJson7.4.3 и LVGL9.5.0. USP_LVGL_ROOT задаёт существующую LVGL.

Проверяется настоящий код задачи и HTTP adapter на synthetic fixtures, JPEG/RGB565, смена альбома, отмена во время GET, отсутствие Bearer/Cookie в CDN request, strict PSRAM, A→B→A и безопасное повторное использование кадров. Main callback возвращает true после смены source и удаления старых cache entries; false сохраняет все используемые кадры для повтора. Main не ждёт cache mutex; worker не получает LVGL pointers.

Один worker владеет transport/decoder, буферы выделяются лениво в PSRAM. При изменении альбома/session старые результаты отвергаются. Stop не ждёт lock. LCD helper работает между refresh в проверенной LVGL OS_NONE/color16 конфигурации; descriptors живут вместе с LCD. Скрытая страница также получает отмену в основном цикле.

Реальные provider запросы и физическое устройство этими тестами не проверяются. Controls остаются disabled. Native/SDK/build результаты отдельны от hardware/authorization acceptance.
