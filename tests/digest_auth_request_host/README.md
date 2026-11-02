# Проверка Digest одного запроса

Запуск: QA Python tests/digest-auth-request-native.py.
Функции разбора и проверки извлекаются из настоящего web_server_idf.cpp,
а callbacks выполняет настоящий AuthMiddleware. HTTP metadata и часы
синтетические; MD5 вычисляет системная libcrypto.so.3, заголовки независимо
подписывает Python hashlib. ASAN/UBSAN включены. Учётные данные тестовые.

Проверяются части тела, upload, final dispatch, запрет повторного запроса,
новый счётчик, неверный пароль, метод/адрес и срок действия.
--expect-duplicate нужен только для сохранённого воспроизведения старого дефекта.
Устройство, Spotify API и реальные credentials не используются.
