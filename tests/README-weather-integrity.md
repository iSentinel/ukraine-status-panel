# Проверки погодной модели

Запуск из корня проекта:

    /root/config/.qa-venv/bin/python3 tests/weather-integrity-native.py

--help выводит справку. Нужны g++, loguru и ArduinoJson 7.4.3. По умолчанию
используется dependency production build; USP_ARDUINOJSON_INCLUDE задаёт
путь к src закреплённой копии. CI получает commit библиотеки из workflow.

Собирается настоящий weather_client.cpp; JSON-библиотека настоящая.
Заменяются только ESPHome preferences, задачи и HTTP/сеть. Подключений
к устройству, записи настроек и настоящих HTTP-запросов здесь нет.
Проверяются некорректные значения, частичные дни, поколения города/поиска,
строки preferences, UTF-8, все256 WMOкодов и переход счётчика времени.
AddressSanitizer/UndefinedBehaviorSanitizer включены по умолчанию.
O0 используется для host-проверки поведения; ESP32-оптимизация проверяется
защищённой production build. --no-sanitize доступен для диагностики среды.

Проверку схемы интервалов выполняют с Python установленного ESPHome;
максимальный интервал —2147483647мс. Принятый реальный ноль не равен
отсутствующему полю. Источник осадков содержит вероятность дождя/снега,
а не объём воды. HTTP-сбой сохраняет только погоду того же города.

Аппаратная приёмка отдельно подтверждает версию, реальные данные/ошибки
источника, UART, Web-файлы и быстрый Home. Native PASS её не заменяет.

## Аппаратная проверка источника

weather-integrity-live-probe.py --address 192.0.2.208 --timeout 45

Запускать вручную после сверки версии. Проверка обновляет погоду того же
города, читает valid/stale/fetch_ok и прогноз, открывает два погодных
экрана через Native API и возвращает Home. Город и настройки не меняются.
Физический палец, многосуточная стабильность и работа отсутствующих
датчиков этой проверкой не подтверждаются. В обычную локальную регрессию
она не включена, чтобы не обращаться к устройству автоматически.

## Согласованный снимок WX-1

weather-snapshot-native.py выполняет production weather_client.cpp
с настоящим ArduinoJson и ASAN/UBSAN. Проверяет независимость копии,
выбор другого города, неудачный запрос, границу freshness, millis wrap
и 10 000 параллельных обновлений. Это проверка кода, не внешний прогноз
и не визуальная приёмка панели. weather-snapshot-static.py проверяет
один сохранённый снимок на все LCD batches и отсутствие отдельных getters.
Старые getters сохранены для совместимости.

## Значения кодов и ширина текста

weather-conditions-native.py проверяет все 256 значений, 29 известных кодов,
UA/RU/EN, неизвестные значения, интенсивность, ледяные осадки/иней и град.
weather-label-width-generated.py --main-cpp PATH --output NEW.json проверяет
короткие подписи по настоящим Glyph advance/ink bounds из сгенерированного
ESPHome C++. Проверка учитывает отсутствующие глифы и размеры LCD полей;
выходной JSON привязан к SHA источников. Это не визуальная приёмка панели.
После production build следует повторить её на main.cpp этой сборки.

## Читаемое время

weather-time-native.py сравнивает общий валидатор с зафиксированными
настоящими функциями принятой .279: 4158 дат и 99944 вариантов времени,
включая границы и високосные годы. Также проверяет три подписи, пустые
и неверные значения. Старый фрагмент в weather-time-legacy.h — только fixture.
weather-time-width-generated.py --main-cpp PATH --output NEW.json измеряет
подписи по фактическим Glyph новой сборки в обоих вариантах экрана.
Данные панели и часы SNTP эти проверки не меняют.

## Язык поиска

weather-geocoder-native.py выполняет production URL builder, request copy,
parser и выбор результата под ASAN/UBSAN. Проверяются 256 языковых значений,
экранирование URL/UTF-8, смена языка при одинаковом query, устаревший ответ,
отсутствие выбора до явного select и 10000 параллельных копий запросов.
Синтетические данные используются только на host; панель не меняется.

## Компактный снимок

weather-current-native.py проверяет равенство текущих полей полного и
компактного view, владение строками, city/freshness/wrap и 10000 параллельных
обновлений каждого типа. Stack high-water-mark на host — явно заданная
RTOS граница, не измерение устройства; реальное значение снимается из UART.
weather-current-static.py проверяет visibility-before-copy для Clock/Room
и сохранение одной полной weather UI transaction.

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
