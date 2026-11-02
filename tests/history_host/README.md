# HistoryStore: нативная проверка

Запуск: python tests/history-subgroup-native.py --help
Затем python tests/history-subgroup-native.py
Можно сохранить полные журналы через --build-dir НОВЫЙ_КАТАЛОГ.

Компилируется полный production history_store.cpp и его настоящий заголовок
с ASAN/UBSAN. Заменены только границы ESPHome: часы, preferences, журнал
и update trigger. Нет подключения к панели, NVS или источникам.
Проверяются 3 языка, группы1..255/подгруппы1..2,16 неверных строк,
дедупликация,10000 событий кольца, старый404-байтовый blob и round-trip.
Fixture history-v1-schedule-group3.bin получена исходным кодом frozen346
до правки E-H3, clock epoch1792962000. SHA256
961c9922035341649e17a74f3765717ef59c0cfc21565c006e3c2e7951ee4e43.
У старой записи подгруппа неизвестна; её значение не восстанавливается
по текущей настройке пользователя.
