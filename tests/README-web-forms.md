# Проверка форм в настоящем браузере

В корне проекта:

    npm ci --prefix tools/web-ui --ignore-scripts
    node tools/web-ui/node_modules/playwright/cli.js install chromium
    bash scripts/test-web-forms.sh

--help выводит справку. NODE_BIN задаёт путь Node, PLAYWRIGHT_BROWSERS_PATH — каталог браузеров.
Все HTTP-ответы и события источника в этих тестах искусственные. Физическая панель не используется.
Проверяются HTTP/сетевая ошибка, сохранение ввода, один клик из активного поля, повторное нажатие,
новый ввод во время отправки, фокус/выделение при обнаружении сущностей и смене языка,
выбор улицы/дома, доступность поиска и отмена устаревшего результата.
Прогоны выполняются для исходного и встроенного JavaScript.
Для npm зависимостей используются закреплённые версии и package-lock.json.
Физическая проверка отдаваемых файлов и совместного backend остаётся отдельной.

R10 coverage runs from the same `scripts/test-web-forms.sh` wrapper against readable and embedded JS. It exercises 8 tabs × 3 languages × 4 responsive widths, real DOM bounds/clipping, >=44 px controls, keyboard traversal and computed semantic color contrast.
