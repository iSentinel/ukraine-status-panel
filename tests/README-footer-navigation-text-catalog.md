# Проверка подписей нижнего меню

Запуск: /root/config/.qa-venv/bin/python3 tests/footer-navigation-text-catalog-native.py

Параметр --build-dir НОВЫЙ_КАТАЛОГ сохраняет исходник проверки, полный журнал
компилятора и результат. --help показывает справку. Старый каталог не заменяется.

Проверяются настоящий заголовок include/usp_footer_text.h, 1280 сочетаний
языка и подписи, неизвестный ключ и 75 мест использования в YAML.
Ожидаемые тексты сохранены в fixtures/footer-navigation-text-catalog-golden.cpp.
Компилятор включает AddressSanitizer и UndefinedBehaviorSanitizer.
Проверка не подключается к устройству и не запускает сборку образа или OTA.

Именованные ключи HOME/MAP/POWER/HISTORY/SETTINGS проверяются непосредственно.
Это подписи 25 сохранённых скрытых кнопок; основная видимая навигация
проверяется отдельно navigation-text-catalog-native.py.
