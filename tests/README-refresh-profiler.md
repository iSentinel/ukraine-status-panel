# Диагностика LVGL refresh
Запуск: /root/config/.qa-venv/bin/python3 tests/lvgl-refresh-profile-native.py
Нужны g++ и Python с loguru. Проверяется настоящий общий C++ header, без LVGL-заглушки.
Покрытие: переход millis через предел, отсутствие ready, ready без render,
несколько refresh в одном handler, 70000 вызовов и ограничение частоты сообщений.

handler_ms = pre_ms + render_ms + post_ms.
pre_ms = dispatch_ms + layout_ms + prepare_ms.
layout_ms включает интервал до последнего UPDATE_LAYOUT_COMPLETED перед первым render;
это не чистое процессорное время layout. render_ms — интервал от первого render
до последнего ready, включая ожидания и другие refresh в этом handler.
Без ready интервал заканчивается возвратом handler, ready_seen=NO.
Это не время до готового физического экрана и не измерение всех действий страницы.
Первые 8 handler и 6 flush сохраняются; поздние медленные записи ограничены
одной в секунду для каждого вида сообщения. Пороги 50/25 мс сохранены.
Отсутствие поздней записи не означает отсутствие медленной операции.
Счётчики uint32 насыщаются, интервалы должны быть короче 49 дней.
Диагностика не вызывает layout/refresh, не меняет сеть, OTA и навигацию.
