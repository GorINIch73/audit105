# audit105 — текущее состояние (чекпоинт)

## Готово

### Этап 0 — каркас (собирается, запускается)
- `CMakeLists.txt`: ImGui (FetchContent, ветка `docking`), `glfw3`, `SQLite3::SQLite3`, `ZLIB`, OpenGL, X11; копирование `data/` в build.
- `src/main.cpp`: GLFW window, ImGui + docking, Roboto с кириллицей + FontAwesome merge, событийный цикл (`glfwWaitEventsTimeout`, FPS cap 60 / idle 15), аргументы `--db`, `--refs`, AboutDialog, статус-бар.
- `src/PlatformUtils.h`, `src/AboutDialog.h`, `src/IconsFontAwesome6.h`, `src/ImGuiFileDialog.*` (из FinAudit).
- `data/Roboto-Regular.ttf`, `data/fa-solid-900.otf`, `data/audit105.svg`, `audit105.desktop`.
- Проверка: `cmake -S . -B build && cmake --build build -j4`, `timeout 4 ./build/audit105` → exit 124 (работает, не падает).

### Этап 1 — App + виджеты
- `src/App.h/.cpp`: меню (Файл, Форма МЗ, Правила, Реестр, Отчёты, Импорт, Вид, Справка), статус-бар, темы, шрифт, реестр views, `OpenDatabase/SaveDatabaseAs/BackupDatabase`.
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

## Этап 5 — Правила MVP (готов, тесты проходят)

- `src/domain/RulesEngine.{h,cpp}`: типы `structural/exact/synonym/term/regex`, порядок применения
  structural → exact → synonym → term → regex; `ApplyRules(runId)` пересчитывает отметки.
- `src/views/RulesView.{h,cpp}`: одна форма с вкладками «Правила / Категории / Термины / Сводка / Очередь»
  (CRUD правил, категорий, терминов; сводка по прогону; очередь нераспознанных + частотные кандидаты в термины).
- `src/views/ReportView.{h,cpp}`: сводка по категориям/субсчётам, реестр помеченных (топ-50), экспорт CSV и HTML,
  печать через `lp`, открытие в браузере.
- `tests/test_rules.cpp` — **проходит**: структурные/терминовые/синонимные/regex правила, пороги, дубликаты.
- `tests/test_views.cpp` — проверяет lifecycle окон (создание/закрытие/Ctrl+W) и рендер RulesView/ReportView/RunsView.

## Этап 6 — Примечания (готов)

- CRUD примечаний в `Database` (`AddNote/UpdateNote/DeleteNote/ListNotes/CountRunNotes`);
  форма позиции в `FormView` показывает правила, арифметику и примечания аудитора
  (накладная №, дата, договор, дата закупки, автор).

## Этап 9 — Реестр прогонов (готов)

- `src/views/RunsView.{h,cpp}`: реестр прогонов с фильтрами (учреждение, период/год, примечание,
  «последние 30 дней»), таблица (файл, формат, учреждение, период, дата импорта, позиции, отмечено),
  действия: открыть форму, удалить прогон, резервная копия, импорт.
- Меню упрощено: одна запись = одно окно (убраны дубли «Справочники»/вкладки Правил).
- `tests/test_views.cpp` проверяет, что RunsView открывается и рендерится.

## Этап 10 — Сравнение прогонов (готов, тесты проходят)

- `src/Database.{h,cpp}`: `CompareRuns(runA, runB)` → `CompareResult`. Сопоставление позиций
  **только по нормализованному названию** (`name_norm`), без объединения данных: для каждого
  нормализованного имени агрегируются остатки и суммы отметок по каждому прогону отдельно.
  В `CompareResult`: `both/onlyA/onlyB/changed`, суммы остатков `sumA/sumB`, отмеченные
  `flaggedA/flaggedB` и их количества, строки отсортированы по убыванию |Δ остатка|.
- `src/views/CompareView.{h,cpp}`: выбор двух прогонов (A базовый / B сравнение), поиск по
  названию, фильтр «только изменившиеся», таблица «Остаток A / Остаток B / Δ / отмечено A/B»
  с цветовой подсветкой динамики, экспорт CSV и HTML, открытие в браузере.
- Меню: «Отчёты → Сравнение прогонов».
- `tests/test_db.cpp` проверяет `CompareRuns`: общее имя, только в B, изменившийся остаток,
  суммы отметок по прогонам, сортировка по Δ.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 11 — XlsxReader и XmlReader (готов, тесты проходят)

- `src/domain/Zip.{h,cpp}` — общие ZIP-хелперы (central directory, inflate) для ODS/XLSX.
- `src/domain/XlsxReader.{h,cpp}`: `ReadXlsx` — sharedStrings, лист `xl/worksheets/sheet1.xml`,
  типы ячеек `s/n/inlineStr/b`, абсолютные колонки из `r="A1"`, merged-ячейки (`mergeCells`) задают span.
- `src/domain/XmlReader.{h,cpp}`: `ReadXml` — два варианта: SpreadsheetML 2003 (`Worksheet/Row/Cell/Data`,
  `ss:Index`, `ss:MergeAcross`) и Flat-ODS (`table:table-row`/`table:table-cell`, как в content.xml).
- `ImportView` теперь выбирает читатель по `DetectFormat` (ODS/XLSX/XML; XLS → конвертация через soffice).
- `tests/test_parser.cpp` читает `105. 2026.xlsx` и `105. 2026.xml` (сгенерированы из ODS через LibreOffice)
  и проверяет 1639 позиций и 0 арифметических сбоев — совпадает с ODS.
- Проверка: `cmake --build build -j4 && ./build/test_parser && ./build/test_db && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 12 — Сравнение по субсчётам и правилам (готов, тесты проходят)

- `src/Database.{h,cpp}`: `CompareSubaccounts(runA, runB)` → агрегация по коду субсчёта
  (остаток A/B, отмеченная сумма A/B, число позиций и отметок); `CompareRules(runA, runB)` →
  агрегация по `rule_code` (сумма и количество отметок в каждом прогоне, категория).
- `src/views/CompareView.{h,cpp}`: три вкладки — «Позиции» (по нормализованному названию),
  «Субсчёты» (динамика остатков и отмеченных сумм по 105.xx), «Правила» (динамика сумм по правилам).
- `tests/test_db.cpp` проверяет `CompareSubaccounts` и `CompareRules` (остатки, суммы отметок,
  количества позиций/отметок по прогонам).
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 13 — Пакет справочников (готов, тесты проходят)

- `src/Database.{h,cpp}`: `ExportDictionaryPackage(path)` — JSON с категориями, правилами и
  терминами; `ImportDictionaryPackage(path, stats)` — upsert по `name`/`code`, термины по
  `term_norm+scope`. Импорт не перезаписывает термины с `origin='manual'`: вместо этого создаётся
  запись в `conflicts`. Минимальный JSON-парсер внутри `Database.cpp` (без внешних зависимостей).
- `src/views/SettingsView.{h,cpp}`: кнопки «Экспорт пакета» (в `~/.audit105/refs_bundle_*.json`)
  и «Импорт пакета» (файловый диалог `.json`), вывод статистики импорта.
- `tests/test_db.cpp`: экспорт → импорт в вторую базу (новая база содержит seed, поэтому
  категории обновляются, новое правило добавляется), повторный импорт не дублирует,
  ручной термин не перезаписывается (конфликт).
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 14 — Формы справочников правил (единый шаблон)

- Общий шаблон для `TermsEditorView` («Термины и списки»), `RulesEditorView` («Правила») и
  `CategoriesEditorView` («Категории»): слева — панель управления над таблицей
  («Добавить», «Удалить», «Пред.», «След.», «Сбросить» + строка выбора), справа — только
  редактор полей с кнопкой «Сохранить».
- «Добавить» создаёт запись сразу в базе с авто-ID и provisional-значениями
  (`Database::CreateBlankRule/Term/Category`), таблица обновляется, новая строка подсвечена
  зелёным и выбрана — пользователь дозаполняет поля и сохраняет.
- `TermsEditorView`: блок «Списки терминов» (группировка по правилу + перечню, счётчики),
  фильтр по перечню, пакетное добавление терминов (текст построчно/через запятую,
  `AddTermsToList` с пропуском дубликатов), колонки «Перечень» и «Дата».
- `RulesEditorView`: для правил типа `term` в редакторе показывается список терминов правила;
  `CategoriesEditorView`: число правил в категории в таблице и в редакторе.
- `tests/test_db.cpp`: проверка `CreateBlank*`, `AddTermsToList`, `ListTermLists`, `CountRuleTerms`.
- Между таблицей и редактором — перемещаемый вертикальный слайдер (`VerticalSplitter`,
  ширина левой панели хранится в `listWidth`); у `ComboWithFilter` убрана кнопка «Выбрать…»
  (попоп открывается кликом по текущему значению).
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 15 — «Добавить копированием» в форме правил

- `RulesEditorView`: в панели управления появилась кнопка «Добавить копированием»
  (`DuplicateDraft`) — дублирует выбранную запись (код + суффикс «-копия», категория, тип,
  шаблон, основание, источник, серьёзность, флаг активности) и выбирает копию.
  Список терминов правила-`term` не копируется (уникальный индекс `term_norm+scope`).
- `Database::DuplicateRule(id)` + `Database::GetRule(id, out)`; `UniqueRuleCode/UniqueCategoryName/
  UniqueTermName` подбирают свободные значения обязательных полей, `FallbackCategoryName`
  создаёт категорию «Черновик», если указанной категории нет.
- Обязательные поля при создании всегда заполнены заглушками: `CreateBlankRule`
  (`новый-<id>`, категория, тип `term`), `CreateBlankTerm` (`новый термин <id>`),
  `CreateBlankCategory` (`Новая категория <id>`); пустой `rule_code` термина теперь
  пишется как NULL (иначе FK `terms.rule_code → rules(code)` блокировал добавление).
- `tests/test_db.cpp`: копирование правила, суффикс «-копия», уникальность кода при
  совпадении, создание пустого правила с неизвестной категорией, термин без правила.
  `tests/test_views.cpp`: `DuplicateDraft()` через открытую форму.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 16 — подтверждение критичных операций

- `ConfirmationModal` был сломан: он открывал обычное окно (`ImGui::Begin`), которое
  висело на экране постоянно, а «Удалить» срабатывало только кликом по этому окну,
  а не по кнопке в панели. Теперь это настоящий `ImGui::BeginPopupModal`:
  окно появляется только после `ImGui::OpenPopup`, закрывается по «Отмена», Esc
  или после подтверждения; ширина фиксирована через `Dummy(320, 0)`.
- Схема во всех вьюерах: клик по кнопке → `OpenPopup(id)` → в том же кадре
  `ConfirmationModal(id, …)`; удаление выполняется только если подтвердили.
  Критичные операции: удаление правила, термина, категории, прогона, примечания.
- `RulesEditorView::DeleteSelected`, `TermsEditorView::DeleteSelected`,
  `CategoriesEditorView::DeleteSelected` теперь сами рисуют кнопку «Удалить»
  (без выбранной записи — сообщение об ошибке), текст подтверждения содержит
  название записи и предупреждение «Отменить это нельзя».
- `tests/test_views.cpp`: проверка, что без клика попап не открыт и ничего не удалено,
  что модалка открывается и имеет нормальный размер, что клик по «Удалить»
  подтверждает операцию, а после подтверждения модалка закрыта.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 17 — «Добавить копированием» в категориях, терминах и списках

- `Database::DuplicateCategory` / `DuplicateTerm` / `DuplicateTermList` (и
  `GetCategory` / `GetTerm` для чтения копии). Имена копий: `«… (копия)»`,
  при конфликте — `«… (копия) 2»` (`UniqueCategoryName`, `UniqueTermName`,
  новый `UniqueListName`).
- Кнопка `ICON_FA_COPY "  Добавить копированием"` добавлена в toolbar
  `CategoriesEditorView` и `TermsEditorView`; в разделе «Списки терминов» —
  кнопка «Скопировать список», которая копирует выбранный перечень вместе
  с его терминами (новое имя `«Перечень (копия)»`).
- Миграция схемы: уникальный индекс `idx_terms_norm` стал мягче —
  `(term_norm, scope, COALESCE(list_name,''))`, чтобы один и тот же термин
  мог жить в нескольких перечнях одного scope. `AddTermsToList` теперь
  проверяет дубликаты в рамках конкретного перечня.
- `DuplicateSelectedList` требует выбранный перечень (клик по строке таблицы
  списков); `SelectList(ruleCode, listName)` фиксирует правило и перечень.
- `tests/test_db.cpp`: копирование категории/термина/списка, суффикс «(копия)»,
  уникальность имён при повторном копировании, удаление копий.
  `tests/test_views.cpp`: `DuplicateDraft()` в формах категорий и терминов,
  `DuplicateSelectedList()` для списка.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 18 — переименование категории (FK `rules.category`)

- Причина ошибки «Не удалось обновить категорию»: `rules.category` ссылается на
  `categories(name)`, поэтому переименование категории, у которой есть правила,
  нарушало FOREIGN KEY (и до, и после `UPDATE`). То же касалось удаления категории
  с правилами.
- `UpdateCategory` теперь в одной транзакции переносит правила на новое имя и
  меняет название категории; контроль FK временно отключается (`PRAGMA
  foreign_keys=OFF`), так внутри транзакции он не работает.
- `DeleteCategory` переносит правила категории в другую (или создаёт «Черновик»)
  перед удалением — раньше удаление категории с правилами падало по FK.
- `Begin/Commit/Rollback` сделаны вложенными (счётчик транзакций), иначе
  `ImportDictionaryPackage` не мог вызвать `UpdateCategory` внутри своей
  транзакции.
- `CategoriesEditorView::SaveDraft` заранее проверяет, что название не занято,
  и сообщает об этом явно; текст подтверждения удаления теперь говорит,
  сколько правил будет перенесено.
- `tests/test_db.cpp`: временная категория с правилом — переименование,
  попытка занять существующее имя (откат), удаление категории и перенос правил.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 19 — связи только по ID

- Все связи в базе теперь по ID: `rules.category_id`, `terms.rule_id`,
  `flags.rule_id`, `items.rule_id`, `items.category_id`. Имена и коды
  (`categories.name`, `rules.code`) используются только для отображения.
- `MigrateToIdRelations()` в `src/Database.cpp` определяет старые колонки
  (`rules.category`, `terms.rule_code`, `flags.rule_code`, `items.rule_code`),
  перестраивает таблицы через `*_new`, переносит связи по имени/коду, создаёт
  категорию-подмену и удаляет флаги без правила.
- `SeedExampleRules` вставляет категории, читает их ID, затем правила с
  `category_id` и термины с `rule_id`.
- API `src/Database.h`: `CreateBlankRule(int64_t categoryId)`,
  `CreateBlankTerm(int64_t ruleId, ...)`, `CountRuleTerms(int64_t ruleId)`,
  `AddTermsToList(int64_t ruleId, ...)`, `DuplicateTermList(int64_t ruleId, ...)`,
  `InsertFlag(itemId, ruleId, severity, amount, rationale)`.
- `UpdateCategory` — простой `UPDATE ... WHERE id=?`; конфликт уникальности
  имени возвращает `false`. `DeleteCategory` переносит правила и позиции в
  категорию-подмену по ID.
- `RulesEditorView` фильтрует и создаёт правила по `categoryFilterId`/`categoryId`;
  `TermsEditorView` — по `ruleFilterId`/`selectedListRuleId`.
- Экспорт словаря сохраняет имена/коды для переносимости, импорт маппит их в ID.
- `tests/test_db.cpp`, `tests/test_rules.cpp`, `tests/test_views.cpp` переведены
  на ID-связи.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 20 — серая категория для «без оборота»

- Добавлена примерная категория «Без оборота» с цветом `#9AA0A6` (серый,
  незаметный); правило `S05` (`no_turnover`) переведено в неё.
- `EnsureSeedCategories()` идемпотентно добавляет недостающие примерные
  категории в уже существующую базу и переносит `S05` в серую категорию.
- Позиция окрашивается по категории правила с наибольшей серьёзностью, поэтому
  «без оборота» (серый) виден только когда других критериев нет.
- `tests/test_db.cpp`: 5 примерных категорий, проверка цвета `S05`.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 21 — правки примерных правил субсчётов

- Удалено правило `SUB02` («перчатки, шприц, игла, бинт, катетер, система
  инфузионная на 105.31 — неверный субсчёт»): медматериалы действительно ведутся
  на 105.31. `EnsureSeedExamples()` удаляет это правило и в уже созданных базах
  (только если это исходный примерный шаблон).
- Добавлено правило `SUB03`: `sub:105.36;реагент, реактив, ивд, питательная
  среда, агар, тест-система, набор` — реагенты и ИВД должны быть на 105.31,
  а не на 105.36.
- `tests/test_rules.cpp`: медматериалы на 105.31 не флагуются, реагенты на
  105.36 флагуются. `tests/test_db.cpp`: позиция «Аланинаминотрансфераза (АЛТ)
  ИВД, набор реагентов» на 105.36 флагуются `SUB03`.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 22 — комбобоксы в терминах и пояснение шаблона в форме правил

- Перечислимые поля формы «Термины» (Scope, Origin, Перечень) и формы «Правила»
  (Тип, Серьёзность, Проверка для structural, Признак дубликата для exact)
  выбираются через `ComboEnum` (`src/Widgets.cpp`); значение вне списка
  подсвечивается подсказкой «не из списка».
- Таблица ключевых слов перенесена в движок: `KeywordHelpFor(type)` в
  `src/domain/RulesEngine.h/.cpp` (`KeywordHelp{name, logic, example, keyword}`).
- В форме «Правила» блок «Пояснение к шаблону» больше не сворачиваемый: он
  рисуется сразу под полями «Субсчёты (sub:)» и телом правила — таблица
  «ключевое слово / логика / пример / вставить», разбор текущего шаблона и
  живая проверка тела (пустое тело, неизвестное ключевое слово, неверное
  регулярное выражение). Кнопка в таблице подставляет ключевое слово в тело.
- `tests/test_rules.cpp`: для каждого типа есть пояснение, каждое ключевое слово
  structural/exact документировано, шаблон не теряется при
  `ParseRulePattern` → `BuildRulePattern`.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 23 — авто-переприменение правил после правок

- `src/App.{h,cpp}`: `MarkRulesChanged()` (счётчик `rulesVersion`),
  `RunRulesVersion(runId)`, `RulesOutOfDate(runId)`, `ReapplyRules(runId)`
  (пересчитывает отметки и обновляет все открытые формы),
  `MarkRunRulesApplied(runId)` — отметка «правила применены» сразу после импорта.
- Правки в «Правилах», «Терминах» и «Категориях» (сохранение, добавление,
  копирование, удаление, пакетная загрузка терминов, примеры правил) помечают
  правила изменёнными.
- `FormView`, `RulesView`, `ReportView`, `CompareView`: при рендере, если
  отметки прогона старше текущих правил, правила переприменяются автоматически
  (с предупреждением); в `FormView` и `RulesView` есть кнопка «Обновить
  отметки» / «Переприменить правила» для ручного обновления.
- `FormView::RefreshData()` сохраняет выбранную позицию при пересчёте.
- `tests/test_views.cpp`: новый прогон не считается устаревшим; после
  `MarkRulesChanged()` форма сама переприменяет правила и позиция получает
  отметку нового правила.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 24 — перечень терминов можно задать вручную

- `Перечень` (`terms.list_name`) — это группировка терминов внутри правила, а не
  признак импорта: движок правил (`EvaluateRules`) использует только `rule_id`,
  перечень нужен для фильтров, таблицы «Списки терминов», пакетного добавления,
  копирования списков и уникальности `term_norm + scope + list_name`.
- В форме термина (`src/views/TermsEditorView.cpp`) рядом с комбоксом существующих
  перечней правила появилась кнопка «+ новый перечень» с полем названия — раньше
  название можно было выбрать только из уже существующих списков (свободный ввод
  был только в пакетном добавлении).
- `SaveDraft()` обрезает название перечня пробелами.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 25 — справочник перечней вместо «видов перечней»

- Справочник `list_kinds` и колонка `terms.list_kind_id` удалены: миграция
  `MigrateTermLists()` в `EnsureSchema` создаёт `term_lists`, переносит в неё
  пары `(rule_id, list_name)` из `terms` (источник и дата — из первого термина),
  пересоздаёт `terms` без `list_kind_id`, удаляет `list_kinds` и индекс
  `idx_terms_kind`. Идемпотентно: повторный запуск ничего не ломает.
- `src/Database.{h,cpp}`: `TermList` = `id, ruleId, ruleCode, name, description,
  sourceDoc, asOfDate, active, sortOrder, termCount, activeTermCount`;
  `ListTermLists`, `GetTermList`, `AddTermList`, `UpdateTermList` (при смене
  имени переносит `terms.list_name`), `DeleteTermList` (удаляет и термины),
  `CreateBlankList`, `DuplicateTermListById`, `EnsureTermList`,
  `AddTermsToList`, `TableExists` (для тестов). Удалены `ListKind`,
  `SeedListKinds`, `ListListKinds`, `AddListKind`, `UpdateListKind`,
  `DeleteListKind`, `GetListKind`, `CreateBlankListKind`, `DuplicateListKind`,
  `FindListKindByName`, `CountListsOfKind`, `CountTermsOfKind`, `SetListKind`.
- Новая форма `src/views/ListsEditorView.{h,cpp}` («Правила → Перечни»):
  фильтры (поиск, правило, только активные), таблица (ID, название, правило,
  описание, источник, дата, терминов, активен), toolbar (добавить,
  копировать, удалить, пред/след), форма (название, правило, описание,
  источник, дата, порядок, активность), блок пакетного добавления терминов
  (текст построчно/через запятую + источник и дата) и переход в форму «Термины».
- `TermsEditorView`: убраны панель «Виды перечней», фильтр по виду, колонка
  «Вид», групповое назначение вида и поля вида в форме термина; выбор перечня
  и групповые операции сохранены.
- Пакет справочников (`ExportDictionaryPackage`/`ImportDictionaryPackage`)
  содержит перечни; отчёт (`Report`) считает `Перечней`.
- Тесты: `test_db` — создание/переименование/дублирование/удаление перечня,
  авто-создание перечня при добавлении термина, количество терминов,
  миграция legacy-базы с `list_kinds` (таблица удалена, источник и дата
  перенесены); `test_views` — `ListsEditorView` открывается, рендерится
  (`lists_list`/`lists_form`) и копирует перечень вместе с терминами.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser && ./build/test_rules && ./build/test_views` → все `OK`.

## Этап 26 (шаг 1) — ЕСКЛП подключён к сборке, меню и тестам
- `CMakeLists.txt`: `CsvReader.cpp`, `RegistryImport.cpp`, `RegistryImportView.cpp` добавлены
  в `audit105`, `test_db`, `test_parser`, `test_views`; новые цели `test_registry` и `esklp_probe`.
- `App.cpp`: меню «Справочники → Федеральные справочники (ЕСКЛП/ГРЛС)» открывает `RegistryImportView`.
- `RegistryImportView.h/.cpp`: добавлен include `Database.h`, починены `ImGui::Combo` (выбор листа),
  иконки и checkbox.
- Fixture из реальной выгрузки: `tools/gen_esklp_fixture.py` → `tests/fixtures/esklp_smnn_sample.xlsx`
  (304 строки: 4 строки заголовка + 300 строк данных с ART/TB/VACC/TEST).
- Исправлено в `RegistryImport.cpp`:
  - `SplitTokens` теперь режет по пробелам (раньше «ЗИДОВУДИН 300 МГ ТАБЛЕТКИ» был одним токеном);
  - `IsAtxToken` принимает 5-уровневые коды АТХ (`J05AF06`) — ART на реальном файле 6 → 76;
  - `ExcelSerialToIso` пересчитан (старая формула давала `2027-08--272`);
  - `RegistryNameVariants` отсекает дозировки, чистые числа и единицы измерения (`300`, `мг`);
  - транслитерация: `y → и`, добавлены `cl`, `cy` (`ACYCLOVIR → ацикловир`).
- `Database.cpp`: `ImportRegistryEntries` накапливает `added`/`conflicts` в `sources`,
  а не перезаписывает их при повторном импорте.
- Тесты: `test_registry` (профиль, фильтры, CSV, импорт, конфликты, удаление источника) — `OK`;
  `test_db`, `test_parser`, `test_rules`, `test_views` — `OK`.

## Этап 26 (шаг 2) — уверенность импорта, связка СМНН→КЛП, регрессия на референсных ODS
- `Database.cpp`: миграция `terms.confidence INTEGER DEFAULT 0`; `confidence` читается в
  `ListTerms`/`GetTerm`, пишется в `AddTerm`/`UpdateTerm` и в `ImportRegistryEntries`
  (при повторном импорте берётся максимум, ручные термины не получают импортной уверенности).
- `RulesEngine.h`: `struct Term` — поле `int confidence = 0`.
- `RegistryImport.cpp`:
  - `MatchLevel` — уровни 3 (АТХ/ОКПД2), 2 (ФТГ), 1 (маска), 0; исключения проверяются по
    `rawName`, `name`, `group`, `atxGroup`, `altNames`;
  - `masksForDevicesOnly` применяется только к записям без АТХ и без ФТГ;
  - `kStopWords` в `RegistryNameVariants` — отсекает `для`, `на`, `типа`, `против`, `живая` и т. п.;
  - `LinkEntriesByNodeCode` — позиция КЛП получает АТХ и ФТГ от узла СМНН по коду узла;
  - `ConfidenceLabel` — текстовая расшифровка уровня.
- `TermsEditorView`: колонка «Уверенность», фильтр «Только импортированные», счётчики
  `importedCount`/`staleCount`, предупреждение об устаревшем справочнике (`staleMonths = 12`).
- `RegistryImportView`: показывает критерии фильтра и уровень уверенности импорта.
- Регрессия в `test_registry.cpp`: прогон `tests/fixtures/105. 2026.ods` → правила → импорт
  всех фильтров ЕСКЛП → правила. Помеченных позиций стало 901 → 914 (не меньше), ложных
  срабатываний на не-лекарственных позициях (бинт, катетер, шприц, инфузионная система) — 0.
- Проверка на реальном файле: ART = 91 (76 АТХ + 15 ФТГ), TB = 110, VACC = 107, TEST = 0.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser &&
  ./build/test_rules && ./build/test_registry && ./build/test_views` → все `OK`.

## Этап 26 (шаг 3) — явная карта колонок и построчный проход полного КЛП
- `RegistryImport.h/.cpp`: `RegistryFormat` (Smnn/Klp/Trade/Group) и `ApplyFormatMap` —
  фиксированная карта колонок ЕСКЛП (МНН, код узла СМНН, АТХ, ОКПД2, ФТГ, РУ, цена, ЖНВЛП,
  дозировка/форма) вместо эвристического поиска aliases; эвристики остаются только fallback.
- `ScanRegistryFile(...)` — построчный проход большого XLSX через `StreamXlsxRows`: в
  `profile.entries` попадают только совпадения с фильтрами, остальные стрки — только счётчики
  (`dataRows`, `skipped`, `linked`). Профиль помечается `matchedOnly`.
- `RegistryImportView::LoadFile` выбирает режим по имени листа/файла: КЛП → построчный проход
  с индексом узлов СМНН из файла связи; остальные форматы — полный профиль. В UI показывается
  строка «Построчный проход большого КЛП: строк …, совпадений …, связано с СМНН …».
- `tools/esklp_probe` получил флаги `--stream` и `--nodes <smnn.xlsx>`.
- Fixture `tests/fixtures/esklp_klp_sample.xlsx` (генератор `tools/gen_klp_fixture.py`):
  многострочная шапка + строка-нумератор + 4 позиции КЛП.
- `tests/test_registry.cpp`: построчный проход КЛП — схема Klp, `dataStartRow=2`,
  `dataRows=4`, материализовано 2 совпадения (из 4), 3 позиции связаны с узлами СМНН,
  ART-позиция получает АТХ `J05A…` от узла.
- Прогон на реальных файлах (`esklp/esklp_klp_*.xlsx`, 9 файлов): 589 326 строк,
  572 999 позиций связаны с узлами СМНН, ART=7538, TB=8757, VACC=2267, TEST=0 —
  без загрузки всей сетки в память.
- Проверка: `cmake --build build -j4 && ./build/test_db && ./build/test_parser &&
  ./build/test_rules && ./build/test_registry && ./build/test_views` → все `OK`.

## Этап: прогресс обработки, журнал событий и «залипание» файлового диалога
- Причина бага: после неудачного разбора `RegistryImportView` держала `parsed = false` и
  вызывала `LoadFile()` каждый кадр (повторный скан огромного файла), а `ImGuiFileDialog`
  оставался открытым с ключом `RegistryFile` — `OpenDialog("ImportFile")` игнорировался,
  `Display("ImportFile")` ничего не рисовал, пока не перезапустить приложение.
- `App` (`src/App.h`, `src/App.cpp`): журнал событий (`LogEvent`, `Events`, `ClearEvents`,
  уровни info/warn/error/success, дедупликация подряд идущих, лимит 300), состояние прогресса
  (`SetProgress`, `ClearProgress`, `HasProgress`) и обёртки диалогов `OpenFileDialog`,
  `ClaimDialog`, `CloseStaleDialogs`.
- Статус-бар (62 px) показывает последнее событие, анимированный/процентный прогресс с
  подписью «идёт обработка, N с — это не зависание» и кнопку «журнал»; отдельное окно
  «Журнал событий» (Вид → Журнал событий) — прокручиваемая история с кнопкой «Очистить».
- `main.cpp` каждый кадр вызывает `app.CloseStaleDialogs({"NewDbDlg", "OpenDbDlg", "SaveDbAsDlg"})`
  — диалог без owning-формы закрывается; idle-режим не срабатывает, пока есть прогресс.
- `RegistryImportView`: чтение файла вынесено в `std::thread` (`StartScan`/`FinishScan`),
  в UI — прогресс по строкам из `<dimension>`, кнопка «Отменить чтение», «Повторить чтение
  файла» вместо автоматического повторного скана; импорт выполняется по перечням
  (`StartImport`/`StepImport`) с прогрессом.
- `ScanProgress` (`src/domain/RegistryImport.h`) передаётся в `ScanRegistryFile` и
  `BuildRegistryProfile`: счётчики строк/совпадений, флаг отмены; `StreamXlsxRows`
  возвращает размер листа через новый параметр `totalRowsOut`.
- `ImportView` больше не разбирает файл каждый кадр: флаг `parseAttempted`, кнопка
  «Повторить чтение файла»; `SettingsView` использует guarded `OpenFileDialog`.
- Тесты: `test_views` — журнал/прогресс, замена устаревшего диалога, watchdog;
  `test_registry` — счётчики прогресса и отмена прохода.

## Этап: отдельное меню «Импорт»

- `src/App.cpp`: меню «Импорт» размещено после «Отчётов» и содержит импорт оборотов, разделитель, импорт
  федерального справочника (ЕСКЛП/ГРЛС), разделитель и disabled-заглушки
  «Импорт правил» / «Экспорт правил» с подписью «в разработке».
- Меню «Справочники» удалено (его пункт перенесён в «Импорт»).
- Подсказки в `RunsView` и `FormView` обновлены на «Импорт → Импорт оборотов».

## Этап: фильтр по правилам и раскраска в форме «Сводка и экспорт»

- `src/views/ReportView.{h,cpp}`: чекбоксы «Правила прогона» (правило, категория, позиции,
  сумма), фильтры по категории, субсчёту, мин. остатку, поиску и «Только отмеченные»;
  строка попадает в выборку, если хотя бы одно её правило отмечено.
- Раскраска как в Форме МЗ: фон строки = цвет категории, красный — арифметика не сходится;
  цветные категории и в сводке, и в колонках «Правило»/«Категория».
- Экспорт работает по фильтру: CSV (одна строка на правило, заголовок с описанием фильтра),
  HTML — сводки по категориям/правилам/субсчётам и подсвеченный реестр для печати.
- `test_views`: проверка фильтра по правилам и наличия фильтрованных строк в CSV/HTML.

## Следующие шаги
1. Smoke-test GUI (нужен дисплей; `xvfb-run` отсутствует).
2. Этап 26 (продолжить, см. `PLAN.md` §15):
   - отдельный источник медизделий для тест-систем (реестр Росздравнадзора / номенклатура МИ);
   - явная карта колонок ЕСКЛП (СМНН/КЛП/ТН/группы) вместо эвристического поиска aliases;
   - прогон фильтров на полном КЛП (≈589 тыс. строк) без загрузки листа в память.
3. Этап 11 (опционально): размеченный ODS, PDF.

## Команды
```
cmake -S . -B build
cmake --build build -j4
./build/test_db
timeout 4 ./build/audit105
```
