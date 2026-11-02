# Отложенная публикация результатов Wi-Fi

ESPHome2026.9.1 передаёт scan listener через main-task event queue. Тест проверяет этот контракт в установленном исходнике; при его изменении требуется повторный разбор. Callback сохраняет ограниченный снимок в отдельный массив864B с ALLOC_EXTERNAL, без fallback в internal heap. Main loop после try-lock публикует его в прежний HTTP-visible массив. HTTP не обращается к staging storage. Два массива и прежний JSON buffer занимают7872B при compile-time cap8192B. При нехватке памяти setup не регистрирует API.

Запуск: ESPHOME_BIN=/opt/esphome/.venv/bin/esphome /root/config/.qa-venv/bin/python3 tests/wifi-scan-results-native.py

Параметры --help, --compiler, --esphome-root. Выполняются11 actual методов, semaphore/radio заменены host dependencies. Проверяются busy300/no-wait/retention/publish-once, unchanged in-flight response, stale/expired/unsolicited, latest snapshot и null mutex.4 полных JSON сравниваются с неизменяемыми образцами actual collector .312 в tests/fixtures/wifi-scan-wire.json. Это синтетические SSID, не реальные сети. Физические радиопоиск, memory exhaustion и contention latency тест не выполняет.

Управление ускоренным режимом Wi-Fi и отказ радио проверяются отдельно
в README-wifi-scan-power.md; здесь эти зависимости заменены для изоляции.
