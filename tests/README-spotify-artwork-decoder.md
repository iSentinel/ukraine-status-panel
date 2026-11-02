# JPEG и память обложек

Запуск: python3 tests/spotify-artwork-decoder-native.py [--help]. Нужны gcc, g++, loguru и установленная LVGL9.5.0; USP_LVGL_ROOT может задавать её каталог. SHA исходников decoder проверяется. Копия tjpgd в репозиторий не включается.

Native ASAN/UBSAN проверяет baseline/grayscale JPEG, пропорции, RGB565 против сохранённого Pillow reference, progressive/oversize/truncated,64 повреждённых входа и отмену. Fixtures синтетические, получены локально; реальные artwork URL не запрашиваются.

Лимиты:128KiB compressed, source до1024px, output до300x300; только baseline JPEG, неподдерживаемый вход оставляет placeholder. Пустые размеры запрещают публикацию частичного кадра. Источник, workspace и frame обязаны не пересекаться.

Пул495168 bytes выделяется лениво только в PSRAM, без внутреннего fallback; проверяются регион/выравнивание, отказ, восстановление и стабильность адресов. Общая внешняя блокировка обязательна. Перед уничтожением остановить writer и снять display loans. В этом этапе пул и decoder ещё не связаны с worker/LCD.

LV_USE_TJPGD=1 включён только в private WEB_RUNTIME block. Public USB профиль остаётся без private Spotify component и флага. Native/ESP compiler/build проверки не являются измерением памяти или визуальной приёмкой устройства. Provider/device/OTA0.
