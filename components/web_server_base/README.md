# Проектное хранение Web credentials

Основа: встроенный web_server_base из ESPHome2026.9.1.
Исходный header SHA256:
1c0e7dc7ad82176a4c6c77774aae001b688faa3da4192b55409f6dcb011939ff.
web_server_base.cpp, __init__.py и fix_rp2040_hash.py.script скопированы
без изменений. Глобальная установка ESPHome не изменялась.

Изменён только ESP32 auth path:
- сервер копирует username/password и не хранит caller-owned указатель;
- set/is_set/snapshot используют один mutex;
- auth middleware работает со своим snapshot вне mutex;
- nullptr и пустые строки передаются authenticate как прежде.
non-ESP32 implementation сохранена под прежними условными ветвями.

Override подключён только в частном Web-профиле. Public USB профиль
удаляет Web runtime markers; его provisioning/OTA audit остаётся отдельным.
Не включать отсутствие auth в этом профиле в доказательство A-F2.

Проверка:
    python3 tests/web-credential-ownership-native.py --help
    python3 tests/web-credential-ownership-native.py
Исполняется настоящий header и cpp; HTTP/ESPHome platform boundaries —
явные doubles. ASAN/UBSAN и controlled threads не являются TSAN или
аппаратной проверкой смены пароля. Действующие secrets не требуются.

## Origin для собственных маршрутов

После успешной аутентификации middleware проверяет present Origin только
для decoded /usp/. Проверка выполняется до request/body/upload callbacks.
Разрешён локальный HTTP Origin, совпадающий с Host и effective port.
Имена хостов сравниваются без учёта регистра, port80 по умолчанию.
Opaque null, другой host/port, userinfo, пути и списки Origin отклоняются403.
Отсутствующий Origin сохраняет совместимость с non-browser клиентами.
Обычные root/SSE/entity handlers сохраняют прежнюю отдельную политику.

Пользователь подтвердил прямое открытие Web по локальному адресу.
HTTPS proxy Origin для /usp/ не разрешён этим локальным HTTP правилом.
Это не замена аутентификации. Реальные credentials не участвуют в offline
tests/web-origin-policy-native.py; HTTP metadata и ответы — контролируемые
границы вокруг actual middleware. IPv6 literal сравнивается по написанию
(без нормализации сокращений адреса). Публичный captive portal не меняется.
