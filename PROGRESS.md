# audit105 — текущее состояние (чекпоинт)

## Готово

### Этап 0 — каркас (собирается, запускается)
- `CMakeLists.txt`: ImGui (FetchContent, ветка `docking`), `glfw3`, `SQLite3::SQLite3`, `ZLIB`, OpenGL, X11; копирование `data/` в build.
- `src/main.cpp`: GLFW window, ImGui + docking, Roboto с кириллицей + FontAwesome merge, событийный цикл (`glfwWaitEventsTimeout`, FPS cap 60 / idle 15), аргументы `--db`, `--refs`, AboutDialog, статус-бар.
- `src/PlatformUtils.h`, `src/AboutDialog.h`, `src/IconsFontAwesome6.h`, `src/ImGuiFileDialog.*` (из FinAudit).
- `data/Roboto-Regular.ttf`, `data/fa-solid-900.otf`, `data/audit105.svg`, `audit105.desktop`.
- Проверка: `cmake -S . -B build && cmake --build build -j4`, `timeout 4 ./build/audit105` → exit 124 (работает, не падает).

### Этап 1 — App + виджеты
- `src/App.h/.cpp`: меню (Файл, Импорт, Форма МЗ, Правила, Отчёты, Справочники, Вид, Справка), статус-бар, темы, шрифт, реестр views, `OpenDatabase/SaveDatabaseAs/BackupDatabase`.
- `src/Widgets.h/.cpp`: `InputTextSync`, `ComboWithFilter`, `HorizontalSplitter`, `VerticalSplitter`, `ConfirmationModal`, `AmountInput`.
- `src/views/BaseView.h`.

### Этап 2 — Database
- `src/Database.h/.cpp`: WAL, FK, схема (`runs, subaccounts, items, flags, notes, categories, rules, terms, kinds, sources, conflicts`), seed категорий, `Backup()`, `SaveAs()`, `IntegrityReport()`, CRUD runs/categories.
- `src/views/SettingsView.h/.cpp` (меню → Справка → Настройки).
- `tests/test_db.cpp` — проходит: `./build/test_db` → `test_db: OK`.
- При первом запуске создаётся `~/.audit105/audit105.db` (проверено: таблицы + 3 категории + WAL).

## Анализ ODS (этап 3) — ключевые факты

Файл: `tests/fixtures/105. 2026.ods` (5.9 MB content.xml, 3314 строк, 1646 строк с «Сумма»).
Разбор через `xml.etree` (не regex! regex ломает merged-cells).

- Таблица одна: `table:table name="Лист_1"`, строки внутри `table:row-group` → использовать `iter(table:table-row)`.
- Абсолютная колонка = сумма `number-columns-repeated * number-columns-spanned` для `table-cell`, и `number-columns-repeated` для `covered-table-cell`.
- Шапка (строка 7): `Счет` 0–3, `Показатели` 7–9, `Сальдо на начало периода` 12–18, `Обороты за период` 25–30, `Сальдо на конец периода` 36–39 (всего 43 колонки).
- Подшапка (строка 8): Дебет 10–12, Кредит 15–18, Дебет 22–24, Кредит 27–29, Дебет 32–34, Кредит 37.
- Позиция = 2 строки: «Сумма» (имя в колонке 0, span 4, rows-spanned 2) и «Кол.» (метка в колонке 4).
- Строки «Сумма» и «Кол.» смещены на 3 колонки: значения «Сумма» начинаются с 12, значения «Кол.» — с 9.
- Число заполненных слотов варьируется (6, 3, 2). Примеры (2026):
  - строка 9: 6 значений → 12–14, 17–20, 24–26, 29–31, 34–36, 39
  - строка 17: 3 значения → 12, 24, 29 (18615.00 − 930.75 = 17684.25)
  - строка 15: 2 значения → 19, 27 (70044.48, 70044.48)
- Числа в ячейках — текст с `\xa0` (неразрывный пробел) как разделитель тысяч и `,` как десятичная точка; часть ячеек также имеет `office:value` (float).
- У части строк имя в колонке 0 пустое (строки 15–26) — это позиции без наименования.

## Следующие шаги
1. `src/domain/Spreadsheet.h` — модель ячеек (abs_col, span, text, numeric) + дедупликация merged-значений.
2. `src/domain/OdsReader.cpp` — zip (CENTRAL DIRECTORY) + zlib `inflate` content.xml, парсинг строк/ячеек.
3. `src/domain/FormatDetector.cpp` — 2025 vs 2026 по шапке.
4. `src/domain/PositionParser.cpp` — слоты, арифметическое тождество (нач + оборот = конечное), дубли, «без оборотов».
5. `tests/test_parser.cpp` — целевые числа из `PLAN.md` (1770 / 1639 / 1641-1646 / 61 / 74 / 1066).
6. Этап 4: `src/views/FormView.*` (таблица, фильтры, цвета, легенда).
7. Этап 5: правила MVP + сводка.

## Команды
```
cmake -S . -B build
cmake --build build -j4
./build/test_db
timeout 4 ./build/audit105
```
