# USP design kit (к документу «Ukraine Status Panel 0.115.240 — разбор кода, дизайна и предложения»)

- `png/` — готовые макеты 1024×600 и сравнения «сейчас / предложение».
- `mockups/` — исходники макетов. Открыть `weather.html`, `details.html`, `states.html`, `home.html`, `other.html` в Chrome.
  Для `home.html` (маски карты) нужен локальный сервер: `python3 -m http.server` в папке `mockups/`, затем http://localhost:8000/home.html.
  `lib.js` — шкала температур (`tcol`), полосы дней (`daily`), почасовая кривая (`hourly`), сборка иконок из слоёв (`icon`).
  `icons.svg` — 15 слоёв иконок погоды (исходник для ESPHome `image`, type GRAYSCALE + alpha → A8, цвет через image_recolor).
  Пересборка: `python3 build.py --fonts fonts --out weather.html --screens screens_weather.html --init init_weather.js`.
- `tools/render_current.py` — реконструкция страниц LVGL из YAML (правила темы LVGL 9.5 по умолчанию):
  `python3 tools/render_current.py firmware/ukraine-status-panel.yaml <корень проекта> out_html --fonts <папка fontsource noto-sans>`
  затем `python3 tools/shot.py out_html/*.html --outdir out_png` (нужен Playwright + Chromium).
- `tools/overflow_audit.py` — список виджетов, обрезанных неявными отступами темы: `python3 tools/overflow_audit.py firmware/ukraine-status-panel.yaml`.
- `current_reconstruction/` — реконструкция всех 12 страниц (начальные значения YAML).
- `findings_registry.md` — сжатый реестр всех находок с файлами и строками.

Все значения на макетах и реконструкциях тестовые. Шрифт Noto Sans — SIL OFL 1.1 (fontsource). Иконки нарисованы заново для проекта.
