# Возврат Spotify авторизации — программный подэтап .320

Native: QA Python с loguru запускает spotify-auth-return-native.py --help
или без аргументов. Компилируются реальные PKCE и Exchange headers с
ASAN/UBSAN. Проверяются code/verifier, одна выдача, replay, неверное состояние,
отказ, malformed/oversize, cancel, обе границы TTL и переполнение millis.
Статический бюджет: Exchange <=2048B, handoff <=1536B.

Browser: NODE_BIN tests/spotify-auth-return-browser.cjs --help / без аргументов.
PLAYWRIGHT_BROWSERS_PATH указывает закреплённый Chromium.
Настоящие popup/postMessage и модули работают на полностью перехваченных
синтетических URL. Три языка, окно/Origin/state, replay, invalid8, ручной перенос,
TTL, публичный HTTP opener и закрытое окно проверяются без реальной сети.
Clipboard заменён локальным объектом теста; системный буфер не изменяется.
Исходные неудачные попытки прототипа сохранены в preflight артефактах.

Exchange сам не потокобезопасен: будущий компонент321 обязан защищать все
доступы одним mutex; основной цикл использует try-lock. Подготовленный PKCE
session потребляется и стирается, включая отказ adopt. Pending code выдаётся
один раз за60с; session имеет прежний TTL5min. TokenRequest нельзя копировать;
его деструктор стирает поля. Никаких NVS, токенов или provider requests здесь нет.

Локальный модуль firmware/webui/usp-spotify-auth-return.js пока не подключён
к основному Web UI. HTTPS callback assets сохранены в web-installer/spotify-callback,
но не опубликованы. Следующий подэтап321: authenticated ESP endpoints, обработка
ручного результата и подключение Web UI к этому контракту. До него работающая
авторизация аккаунта не заявляется, controls Spotify остаются disabled.

Для последующего хостинга: HTTPS, отсутствие query logging для callback path,
Referrer-Policy:no-referrer, Cache-Control:no-store, frame-ancestors:none,
никакого COOP same-origin на callback, никаких сторонних скриптов. Адрес redirect
должен точно совпадать с регистрацией Spotify. Если opener потерян, есть ручной
одноразовый перенос code/state, но verifier никогда не покидает панель.
Ограничение code1024B fail-closed: больший ответ отвергается целиком.
