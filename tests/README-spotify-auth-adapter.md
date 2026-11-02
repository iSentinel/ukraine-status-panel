# Private Spotify auth HTTP adapter — .321

QA Python с loguru запускает spotify-auth-adapter-native.py --help / без аргументов.
Нативно компилируется настоящий components/usp_spotify_auth/spotify_auth.cpp.
Только SDK, часы, allocator и platform RNG заменены fixtures из spotify_auth_host/.
ASAN/UBSAN: body4096, chunks включая1B, strict form, wrong state/replay/denial,
expiry, one-time handoff, RNG/PSRAM/base failure, auth-on/off, concurrency1000
begin/callback/cancel/handoff параллельно main loop. Настоящий esp_fill_random
не исполняется на host; он входит в обязательную целевую ESP32 проверку.

spotify-auth-adapter-static.py исполняет фактический validator на6 конфигурациях
и проверяет private/public границу. web-origin-policy-native.py дополнительно
проверяет4 новых route через настоящий AuthMiddlewareHandler: auth first,
свой Origin разрешён, чужой403, без успешной аутентификации401 до чтения headers.
Это отдельная проверка middleware, а не тест действующего LAN устройства.

GET /usp/spotify/auth/status; POST begin/callback/cancel.
POST Content-Type:text/plain;charset=UTF-8, тело form-encoded, query запрещён.
begin: client_id, redirect_uri. callback: state + code либо error.
cancel: пустое тело. Любые aliases/duplicates/extra fields отвергаются.
Каждый ответ no-store/no-referrer; code/verifier никогда не попадают в JSON.

Body4097B выделяется только в PSRAM. Exchange и platform RNG находятся под одним
mutex; основной цикл и HTTP/worker используют try-lock. Body хранится только
в сериализованном HTTP task и стирается после запроса. Регистрация публикуется
через atomic flag. В public config компонент, helper form и внешний список
полностью удаляются WEB_RUNTIME markers; генератор имеет дополнительные запреты.

Это ещё не вход в аккаунт: конфигурационная Web-страница и token worker следуют
отдельно. Реальные Client ID, токены, provider I/O, сохранение настроек,
OTA и контакт с панелью при этих проверках не используются.
