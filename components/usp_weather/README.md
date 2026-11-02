# Данные погоды

WeatherClient::snapshot() возвращает независимую копию текущих данных:
город, страну, температуру, влажность, ветер, погодный код и доступные дни.
valid, fetch_ok и stale_latched описывают разные состояния качества.
При valid=false показывайте заглушки; не подставляйте нули как прогноз.
Проверяйте forecast_count перед чтением дней. Срок актуальности вычисляется
под блокировкой при получении снимка; однажды устаревший прогноз не оживает
после переполнения часов без нового успешного обновления.

Для одного обновления экрана получайте один снимок. Он владеет строками
и сохраняется неизменным при следующем запросе или выборе другого города.
condition_text(code, language) форматирует код из этого же снимка.
Старые getters совместимы, но несколько вызовов могут читать разные поколения.

Порядок блокировок snapshot и writer одинаковый: город, затем данные погоды.
Сетевые операции не выполняются под этими блокировками. LCD хранит один
непостоянный weather_ui_snapshot на всю последовательность обновлений
с паузами. Он не записывается в NVS.
Проверки описаны в tests/README-weather-integrity.md.

## Коды и подписи

include/usp_weather_codes.h содержит определения кодов Open-Meteo/WMO.
Полная подпись сохраняет явление и интенсивность. Короткие подписи
используются в тесных LCD полях; полный текст не заменяется во внутренних
данных. Признаки ice_related и hail следуют только из кода источника.
ice_related включает также туман с отложением изморози (48). Для 96/99
intensity относится к граду; силу самой грозы эти коды не задают.
Код 97 означает сильную грозу. Неизвестные коды остаются неизвестными.
Источники: https://open-meteo.com/en/docs, таблица проверена 23.10.2026.

## Время погодных значений

current.time хранится как местное время выбранного города (timezone=auto
в существующем запросе). Это время значений погоды, не время загрузки.
LCD показывает «Дані на / Данные на / As of» и DD.MM.YYYY HH:MM.
Секунды скрываются. Часовой пояс и дата не пересчитываются через часы панели.
Строгий общий valid_date/valid_timestamp из usp_weather_time.h используется
и транспортом, и отображением. Пустое/неверное время показывается как --.

## Язык явного поиска

request_city_search(query, language) принимает язык меню 0/1/2 как uk/ru/en.
Язык копируется в SearchRequest вместе со строкой и поколением запроса под
query_mutex_. Новый поиск той же строки на другом языке отменяет старый
ответ по generation. Старые callers без аргумента сохраняют английский.
Неизвестный язык безопасно заменяется en.

LCD/Web backend передаёт текущий UI язык только при явном запросе поиска.
Смена UI языка сама не запускает поиск и не выбирает город. Имя выбранного
города остаётся из результата, выбранного пользователем. Провайдер может
вернуть английское или местное имя, если перевода нет.
Документация: https://open-meteo.com/en/docs/geocoding-api.

## Компактное чтение и бюджет памяти

current_snapshot() возвращает только текущие значения, качество и число
дней прогноза; массив дней не копируется. snapshot() возвращает полный
независимый прогноз. Оба метода используют одну copy_snapshot_ и одинаковый
порядок блокировок/правило устаревания. Clock/Room проверяют видимость
до чтения. Полный view сохраняется один раз на всю weather UI transaction.

На ESP32-S3 текущие типы занимают 104 и 264 байта соответственно
(в измеренной .281 основе). При расширении модели размер нужно измерить
повторно. В WEATHER журнале stack_free_b — минимум свободных байт стека
worker за время его работы, а не текущий размер свободной памяти.
Число пересчитывается через sizeof(StackType_t); до setup доступно 0.
Стек остаётся 12 КБ в PSRAM. Метрика не меняет запросы или настройки.

Размер WeatherCurrent ограничен compile-time budget 192 байта; массивы
прогноза должны оставаться в полном снимке. На текущем ESP32-S3 view=104B.

### WX-4B forecast storage
The snapshot owns at most24 hourly and7 daily rows in fixed arrays.
forecast_hours=25 bounds the provider reply independently of forecast_days=7;
the parser skips points before current provider time and keeps at most24.
No timestamp is converted using a guessed future UTC offset. Daily missing
rain/UV is NaN, missing sunrise/sunset is empty, and incomplete hourly rows
are skipped. Consumers must display missing optional values as unavailable.
Existing four forecast cards are unchanged; this is the data model stage.

### WX-4C shared weather text
include/usp_weather_view.h owns the current display strings and four DayView
cards, capturing one locale and provider sequence. Both LCD variants read
this view; fullscreen never reads text from ordinary LVGL widgets.
weather-view-native.py compares26 dynamic labels with the captured .283
YAML lambdas over49152 cases (WMO256,UA/RU/EN,quality8,count0..7),
then checks owning strings and sequence/locale lifetime. Platform services
use controlled host substitutes; hardware visual acceptance is separate.
weather-snapshot-static.py binds actual ordinary/fullscreen writes to one
view transaction while preserving yields and the full provider snapshot.

### WX-5A provider time context
Owning compact/full snapshots preserve bounded provider timezone and explicit
whole-minute UTC offset (UTC-12..UTC+14) as one publication. Missing/invalid
metadata stays unknown and does not invalidate otherwise good weather.
valid_name checks identifier shape, not a timezone database. The offset
helper labels UTC+HH:MM; it never guesses future civil time or DST rules.
No fallback to panel/selected-city timezone is applied to missing metadata.
weather-time-context-native.py covers types/bounds/UTC0, owning copies,
city reset and10000 concurrent publications with actual parser/ArduinoJson.

The shared JSON text reader preserves ArduinoJson's exact string length.
Embedded NUL/control bytes cannot become an accepted shortened timezone,
timestamp, daily date or city name. Metadata identifiers are bounded before
allocation. Native negative cases reproduce the old truncation and require
rejection while keeping valid payload behavior unchanged.
