# Проверка настройки кэша LCD

RGB bounce-buffer переносит строки изображения из PSRAM через кэш процессора.
Для этого режима Espressif рекомендует строку кэша 64 байта. Общий размер кэша
остаётся 32 КиБ, поэтому настройка не забирает дополнительные 32 КиБ у приложения.

Проверка исходных production/public настроек:

    python3 tests/rgb-cache-line-config.py

После завершённой production build проверить фактический результат:

    python3 tests/rgb-cache-line-config.py --sdkconfig firmware/.esphome/build/ukraine-status-panel/sdkconfig.ukraine-status-panel

Справка: добавьте --help. Скрипт не подключается к панели и ничего не меняет.
Он не доказывает отсутствие дрожания: требуется осмотр физического экрана.
Разбор причины и критерии: docs/reviews/2026-10-20-DISPLAY_JITTER_ANALYSIS.md.
