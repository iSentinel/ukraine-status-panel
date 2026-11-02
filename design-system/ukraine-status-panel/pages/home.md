# Главный экран: шапка состояния

Этап .391 сохраняет карту и карточки на прежних местах. В верхней строке
разделены название, дата, время, значок сигнала Wi-Fi, подпись питания,
состояние питания и язык. Между областями остаётся не менее 8 пикселей.
Подписи питания проверяются для UA/RU/EN. Значок Wi-Fi сохраняет
существующую отрисовку уровня сигнала и отсутствия подключения.

Время до синхронизации остаётся `--:--:--`, дата — `--.--.----`.
Цвета и значения питания берутся из существующего обработчика.
Изменения не добавляют сетевых запросов, таймеров или буферов экрана.

Проверки:
- `tests/home-header-layout.py`: границы и отсутствие пересечений.
- `tests/home-header-width-generated.py --main-cpp PATH --output PATH`:
  реальные шрифты, 86400 вариантов времени, даты, подписи и неизвестные
  числовые статусы питания.
- Полная проверка проекта и отдельная визуальная проверка самой панели.

Автоматическая проверка размеров не заменяет оценку пользователем.

## Home 2.0 / 0.115.402-dev

Home now prioritizes immediate decisions over technical inventory. The map and
alert surfaces keep their validated geometry. The power card exposes four
primary states only: mains, Shelly EM-50, battery voltage and Wi-Fi. The Yasno
group remains on the outage card instead of being duplicated in power.

The lower-left Home surface is an environment overview: current Weather and
Room state. Detailed event history remains available on the dedicated History
page. Missing SHT41/SHT45 hardware is shown explicitly as not connected; no
synthetic room values are produced.

This slice adds no provider polling, Shelly RPC, framebuffer, storage or
navigation path. It only reuses already-owned snapshots and sensor state.
