# Проверки транспорта обложки Spotify

Запуск: python3 tests/spotify-artwork-transport-native.py [--compiler g++].
Нужны g++ и Python с loguru. Production CPP/header проверяются напрямую с ASAN/UBSAN, заменяются только ESP-IDF/network/gate зависимости.

Проверяются: фиксированный разрешённый CDN origin, HTTPS bundle, no redirect, отсутствие Authorization/cookies, deadline, bounded caller buffer, отмена, failures, полный CDN Retry-After и no-bundle отказ.

Компонент ещё не подключён к worker и LCD. Реальных запросов и аппаратной приёмки нет. Worker должен сохранять transport instance между альбомами/logout, передавать собственный Request и строгий PSRAM buffer, а current callback должен проверять актуальность потокобезопасно. Прототип JPEG decoder находится отдельно в artifacts.
