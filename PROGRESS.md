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

## Этап 3 — парсер ODS (готов, тесты проходят)

- `src/domain/Utf8.{h,cpp}` — UTF-8-функции: `Utf8Decode`, `Utf8Encode`, `ToLowerUtf8`, `ToUpperUtf8`,
  `TrimUtf8`, `CollapseSpacesUtf8`, `NormalizeTextUtf8`, `ContainsUtf8`, `EqualsUtf8Insensitive`.
  Все сравнения/тримминг/числовые нормализации идут через них (никакого `std::tolower` по байтам).
  Правильно обрабатываются: `\u00A0` (nbsp), `\u2000–\u200B`, `\u2212` (минус), Cyrillic А–Я/а–я, Ё (U+0410/U+0450),
  Latin-1 Supplement, Latin Extended-A (включая Ÿ→ÿ, İ→i).
- `src/domain/OdsReader.cpp` — zip (CENTRAL DIRECTORY) + zlib inflate, абсолютные колонки (span/repeat, covered-cells),
  `DecodeEntities` (в т.ч. числовые entity → UTF-8), `StripTags`, `TrimUtf8`.
- `src/domain/FormatDetector.cpp` — ODS/XLSX/XLS/XML по magic bytes + расширению.
- `src/domain/PositionParser.cpp` — шапка (первая строка с 3×«Дебет» и 3×«Кредит») задаёт слоты 7,10,14,17,20,23;
  подитоги `105.xx` отдельно; пары «Сумма»/«Кол.»; медианная цена; точные дубли (имя+субсчёт+все 6 сумм)
  и дубли по имени; без оборота; отрицательные остатки.
- `tests/test_parser.cpp` — **проходит**: `./build/test_parser` → `test_parser: OK`.

### Проверенные числа (реальные, а не из PLAN)
| файл | строк | позиций | «Сумма» | «Кол.» | подитогов | арифм. сбоев | точных дублей | дублей по имени | без оборота |
|---|---|---|---|---|---|---|---|---|---|
| 105. 2026.ods | 3314 | 1639 | 1639 | 1646 | 7 | 0 | 1 | 38 | 736 |
| 105. 2025.ods | 3576 | 1770 | 1770 | 1777 | 7 | 0 | 2 | 44 | 385 |

Расхождение с PLAN.md: «1641/1646 разрешимых», «61/74 дублей», «1066 без оборота» получены ранним
regex-анализом; при корректном разборе merged-cells все 1639 позиций сходятся арифметически, точных
дублей 1–2, без оборота 736/385. Целевые значения в тестах — из таблицы выше.

## Этап 4 — ImportView + FormView (готов, собирается)

- `src/views/ImportView.{h,cpp}`: ImGuiFileDialog (`.ods, .xlsx, .xls, .xml`), `DetectFormat`,
  конвертация `.xls/.xlsx` через `soffice --headless --convert-to ods` (если LibreOffice нет — понятная ошибка),
  профиль импорта (позиции, подитоги, дубли, без оборота, остаток), поля организация/период/примечание/счёт/КФО,
  кнопка «Импортировать в базу» → `Database::ImportRun`.
- `src/views/FormView.{h,cpp}`: выбор прогона, фильтры (поиск по UTF-8, субсчёт, режим оборота, мин. остаток),
  таблица 11 колонок, подсветка строк (красный — арифметика, жёлтый — без оборота, синий — правило), легенда,
  итог «показано N из M / сумма остатков».
- `src/Database.{h,cpp}`: `ImportRun` теперь возвращает `runId`; добавлены `ListRunItems(runId)` →
  `std::vector<ItemRecord>` и `ListSubaccounts(runId)`.
- Меню: «Импорт → Импорт оборота» и «Форма МЗ → Таблица позиций» больше не заглушки.
- `tests/test_parser.cpp` проверяет round-trip: импорт в SQLite и чтение обратно (1770 позиций, остаток сходится).
- Проверка: `cmake --build build -j4 && ./build/test_parser && ./build/test_db` → оба `OK`.

## Следующие шаги
1. Этап 5: правила MVP (structural: арифметика/без «Кол.»/отрицательные остатки; exact: дубликаты) + сводка.
2. XlsxReader (тот же GridModel), XmlReader.
3. Smoke-test GUI (нужен дисплей; `xvfb-run` отсутствует).

## Команды
```
cmake -S . -B build
cmake --build build -j4
./build/test_db
timeout 4 ./build/audit105
```
