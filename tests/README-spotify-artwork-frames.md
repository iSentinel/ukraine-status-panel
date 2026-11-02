# Проверка владения кадрами

Запуск: python3 tests/spotify-artwork-frames-native.py [--compiler g++]. Нужны g++ и Python с loguru. Native ASAN/UBSAN: 10000 замен, stale writer, logout, wrong slot/ticket и повторный acknowledge.

Два pixel buffers принадлежат вызывающему коду; модель хранит <=48 байт метаданных. Tickets уникальны из RequestPlan; счётчик не пересоздавать при живых кадрах. Все методы требуют одной внешней блокировки. Main держит её через замену LVGL source/cache invalidation и затем acknowledges RETIRED. Worker не освобождает DISPLAYED/RETIRED; отменённый WRITING освобождается только после finish. При select(0) main скрывает source через detach/cache-drop/ack. Пока модель не связана с worker/LCD и не выделяет PSRAM. Аппаратная приёмка не заявляется.
