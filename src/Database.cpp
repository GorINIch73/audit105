#include "Database.h"

#include "PlatformUtils.h"
#include "domain/Utf8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace {

std::string NowIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string ColumnText(sqlite3_stmt* st, int col) {
    const unsigned char* text = sqlite3_column_text(st, col);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

std::string NowCompact() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

struct JsonValue {
    enum Type { Object, Array, String, Number, Bool, Null } type = Null;
    std::string str;
    double num = 0.0;
    bool boolean = false;
    std::vector<std::pair<std::string, JsonValue>> obj;
    std::vector<JsonValue> arr;
    const JsonValue* get(const std::string& key) const {
        for (const auto& p : obj) if (p.first == key) return &p.second;
        return nullptr;
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        const JsonValue* v = get(key);
        return (v && v->type == String) ? v->str : def;
    }
    int getInt(const std::string& key, int def = 0) const {
        const JsonValue* v = get(key);
        return (v && v->type == Number) ? static_cast<int>(v->num) : def;
    }
    bool getBool(const std::string& key, bool def = true) const {
        const JsonValue* v = get(key);
        return (v && v->type == Bool) ? v->boolean : def;
    }
};

void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
}

bool ParseJsonString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    int cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp += h - '0';
                        else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                        else return false;
                    }
                    if (cp < 0x80) out.push_back(static_cast<char>(cp));
                    else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return false;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool ParseJsonValue(const std::string& s, size_t& i, JsonValue& out);

bool ParseJsonObject(const std::string& s, size_t& i, JsonValue& out) {
    out.type = JsonValue::Object;
    i++;
    SkipWs(s, i);
    if (i < s.size() && s[i] == '}') { i++; return true; }
    while (true) {
        SkipWs(s, i);
        std::string key;
        if (!ParseJsonString(s, i, key)) return false;
        SkipWs(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        i++;
        JsonValue val;
        if (!ParseJsonValue(s, i, val)) return false;
        out.obj.emplace_back(key, std::move(val));
        SkipWs(s, i);
        if (i >= s.size()) return false;
        if (s[i] == ',') { i++; continue; }
        if (s[i] == '}') { i++; return true; }
        return false;
    }
}

bool ParseJsonArray(const std::string& s, size_t& i, JsonValue& out) {
    out.type = JsonValue::Array;
    i++;
    SkipWs(s, i);
    if (i < s.size() && s[i] == ']') { i++; return true; }
    while (true) {
        JsonValue val;
        if (!ParseJsonValue(s, i, val)) return false;
        out.arr.push_back(std::move(val));
        SkipWs(s, i);
        if (i >= s.size()) return false;
        if (s[i] == ',') { i++; SkipWs(s, i); continue; }
        if (s[i] == ']') { i++; return true; }
        return false;
    }
}

bool ParseJsonValue(const std::string& s, size_t& i, JsonValue& out) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '{') return ParseJsonObject(s, i, out);
    if (c == '[') return ParseJsonArray(s, i, out);
    if (c == '"') { out.type = JsonValue::String; return ParseJsonString(s, i, out.str); }
    if (c == 't') { if (s.compare(i, 4, "true") == 0) { i += 4; out.type = JsonValue::Bool; out.boolean = true; return true; } return false; }
    if (c == 'f') { if (s.compare(i, 5, "false") == 0) { i += 5; out.type = JsonValue::Bool; out.boolean = false; return true; } return false; }
    if (c == 'n') { if (s.compare(i, 4, "null") == 0) { i += 4; out.type = JsonValue::Null; return true; } return false; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        size_t start = i;
        while (i < s.size() && (s[i] == '-' || s[i] == '+' || (s[i] >= '0' && s[i] <= '9') ||
                               s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
        out.type = JsonValue::Number;
        try { out.num = std::stod(s.substr(start, i - start)); }
        catch (...) { return false; }
        return true;
    }
    return false;
}

struct SeedCategory {
    const char* name;
    const char* color;
    const char* description;
    int sortOrder;
};

struct SeedRule {
    const char* code;
    const char* category;
    const char* type;
    const char* pattern;
    int severity;
    const char* rationale;
    const char* sourceDoc;
};

struct SeedTerm {
    const char* ruleCode;
    const char* term;
    const char* scope;
    const char* origin;
    const char* listName;
    const char* sourceDoc;
};

// Примеры правил, которые добавляются при создании новой базы.
// Логика: structural/exact ловят технику ведения счёта, term — перечни МЗ (не ОМС),
// synonym — неверный субсчёт, synonym/regex/structural — кандидаты в основные средства (ОС).
const SeedCategory kSeedCategories[] = {
    {"Недопустимо по ОМС", "#C0392B", "Закупка не финансируется ОМС (перечни МЗ)", 1},
    {"Неверный субсчёт", "#E67E22", "Позиция не на том субсчёте 105", 2},
    {"Кандидаты в ОС", "#2C7FB8", "Позиции, которые должны закупаться как основные средства", 3},
    {"Обратить внимание", "#F1C40F", "Требует ручной проверки документов", 4},
    {"Без оборота", "#9AA0A6", "Позиции без оборота за период — сами по себе не требуют внимания", 5},
};

const SeedRule kSeedRules[] = {
    // 1. structural — техника ведения счёта (арифметика, структура строки)
    {"S01", "Обратить внимание", "structural", "arith", 3,
     "Сальдо не сходится: начало + оборот != конец", "Внутренняя проверка арифметики"},
    {"S02", "Обратить внимание", "structural", "qty_missing", 2,
     "Нет строки «Кол.» — количество неизвестно", "Внутренняя проверка структуры"},
    {"S03", "Обратить внимание", "structural", "qty_mismatch", 1,
     "Слоты «Сумма» и «Кол.» заполнены неравномерно", "Внутренняя проверка структуры"},
    {"S04", "Обратить внимание", "structural", "negative", 3,
     "Отрицательный остаток на конец периода", "Внутренняя проверка"},
    {"S05", "Без оборота", "structural", "no_turnover", 1,
     "Позиция без оборота за период", "Внутренняя проверка"},
    {"S06", "Обратить внимание", "structural", "zero_qty", 2,
     "Сумма есть, количество нулевое", "Внутренняя проверка"},

    // 2. exact — дубликаты
    {"S07", "Обратить внимание", "exact", "signature", 3,
     "Точный дубликат позиции (совпадают все суммы)", "Внутренняя проверка"},
    {"S08", "Обратить внимание", "exact", "name", 2,
     "Дубликат наименования на том же субсчёте", "Внутренняя проверка"},

    // 3. term — перечни ЛС, которые закупает МЗ, а не ОМС.
    //    Список терминов живёт в справочнике «Термины и списки» и редактируется построчно.
    {"OMS01", "Недопустимо по ОМС", "term", "sub:105.31;", 3,
     "Вакцины, анатоксины, иммуноглобулины и сыворотки — федеральный перечень МЗ, ОМС их не оплачивает",
     "Пример перечня МЗ (сверить с планом финансирования)"},
    {"OMS02", "Недопустимо по ОМС", "term", "sub:105.31;", 3,
     "АРТ-препараты при ВИЧ/СПИД — федеральная программа МЗ, не ОМС",
     "Пример перечня МЗ (сверить с планом финансирования)"},
    {"OMS03", "Недопустимо по ОМС", "term", "sub:105.31;", 3,
     "Препараты для лечения туберкулёза — федеральная программа МЗ, не ОМС",
     "Пример перечня МЗ (сверить с планом финансирования)"},
    {"OMS04", "Недопустимо по ОМС", "term", "sub:105.31;", 2,
     "Препараты наркологии (кодирование, заместительная терапия) — вне ОМС",
     "Пример перечня МЗ (сверить с планом финансирования)"},
    {"OMS05", "Недопустимо по ОМС", "term", "sub:105.36;", 2,
     "Тест-системы и ИВД для скрининга ВИЧ и гепатитов — федеральный бюджет, не ОМС",
     "Пример перечня МЗ (сверить с планом финансирования)"},

    // 4. synonym — немедицинское и медизделия на субсчёте лекарственных препаратов
    {"SUB01", "Неверный субсчёт", "synonym",
     "sub:105.31;автозапчасти, бензин, дизель, краска, цемент, штукатурка, шпаклевка, гвозди", 2,
     "На 105.31 учитываются только лекарственные препараты: немедицинские позиции — ошибка субсчёта",
     "Профиль субсчёта 105.31"},
    {"SUB03", "Неверный субсчёт", "synonym",
     "sub:105.36;реагент, реактив, ивд, питательная среда, агар, тест-система, набор", 2,
     "Реагенты, ИВД и питательные среды — медицинские материалы, их ведут на 105.31, а не на 105.36",
     "Профиль субсчёта 105.31"},

    // 5. кандидаты в ОС (основные средства): synonym, regex и порог оборота
    {"OS01", "Кандидаты в ОС", "synonym",
     "sub:105.34,105.35;оборудование, аппарат, монитор, кровать, стол, шкаф, стул, кресло, инвентарь", 2,
     "Оборудование, мебель и инвентарь закупаются как основные средства, а не по счёту 105",
     "Порядок учёта основных средств"},
    {"OS02", "Кандидаты в ОС", "regex",
     "sub:105.34,105.35,105.36;оборудование|аппарат|монитор|инвентарь|мебель|прицеп|тележка", 2,
     "Regex-пример: признаки ОС (техника, мебель, транспорт) на субсчётах МЗ",
     "Порядок учёта основных средств"},
    {"OS03", "Кандидаты в ОС", "structural", "sub:105.34,105.35;no_turnover:50000", 1,
     "Нет оборота и остаток выше порога — признак длительного использования (ОС)",
     "Порядок учёта основных средств"},
};

const SeedTerm kSeedTerms[] = {
    {"OMS01", "вакцин", "global", "manual", "МЗ: вакцины и иммуноглобулины", "Пример перечня МЗ"},
    {"OMS01", "анатоксин", "global", "manual", "МЗ: вакцины и иммуноглобулины", "Пример перечня МЗ"},
    {"OMS01", "иммуноглобулин", "global", "manual", "МЗ: вакцины и иммуноглобулины", "Пример перечня МЗ"},
    {"OMS01", "сыворотк", "global", "manual", "МЗ: вакцины и иммуноглобулины", "Пример перечня МЗ"},
    {"OMS01", "антираби", "global", "manual", "МЗ: вакцины и иммуноглобулины", "Пример перечня МЗ"},

    {"OMS02", "антиретровирусн", "global", "manual", "МЗ: АРТ при ВИЧ", "Пример перечня МЗ"},
    {"OMS02", "невиропин", "global", "manual", "МЗ: АРТ при ВИЧ", "Пример перечня МЗ"},
    {"OMS02", "зидовудин", "global", "manual", "МЗ: АРТ при ВИЧ", "Пример перечня МЗ"},
    {"OMS02", "ламивудин", "global", "manual", "МЗ: АРТ при ВИЧ", "Пример перечня МЗ"},
    {"OMS02", "лопинавир", "global", "manual", "МЗ: АРТ при ВИЧ", "Пример перечня МЗ"},
    {"OMS02", "эфавиренц", "global", "manual", "МЗ: АРТ при ВИЧ", "Пример перечня МЗ"},

    {"OMS03", "туберкулез", "global", "manual", "МЗ: противотуберкулезные ЛС", "Пример перечня МЗ"},
    {"OMS03", "изониаз", "global", "manual", "МЗ: противотуберкулезные ЛС", "Пример перечня МЗ"},
    {"OMS03", "рифампицин", "global", "manual", "МЗ: противотуберкулезные ЛС", "Пример перечня МЗ"},
    {"OMS03", "пиразинамид", "global", "manual", "МЗ: противотуберкулезные ЛС", "Пример перечня МЗ"},
    {"OMS03", "этамбутол", "global", "manual", "МЗ: противотуберкулезные ЛС", "Пример перечня МЗ"},

    {"OMS04", "налтрексон", "global", "manual", "МЗ: наркология", "Пример перечня МЗ"},
    {"OMS04", "налоксон", "global", "manual", "МЗ: наркология", "Пример перечня МЗ"},
    {"OMS04", "метадон", "global", "manual", "МЗ: наркология", "Пример перечня МЗ"},
    {"OMS04", "дисульфирам", "global", "manual", "МЗ: наркология", "Пример перечня МЗ"},
    {"OMS04", "тетурам", "global", "manual", "МЗ: наркология", "Пример перечня МЗ"},

    {"OMS05", "вич", "global", "manual", "МЗ: тест-системы ВИЧ/гепатиты", "Пример перечня МЗ"},
    {"OMS05", "гепатит", "global", "manual", "МЗ: тест-системы ВИЧ/гепатиты", "Пример перечня МЗ"},
    {"OMS05", "тест-система", "global", "manual", "МЗ: тест-системы ВИЧ/гепатиты", "Пример перечня МЗ"},
    {"OMS05", "иммунофермент", "global", "manual", "МЗ: тест-системы ВИЧ/гепатиты", "Пример перечня МЗ"},
};

const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS runs(
  id INTEGER PRIMARY KEY,
  file TEXT NOT NULL, format TEXT,
  organization TEXT,
  period TEXT,
  note TEXT,
  account TEXT DEFAULT '105', kfo TEXT DEFAULT '7',
  imported_at TEXT NOT NULL,
  items INTEGER DEFAULT 0, flagged INTEGER DEFAULT 0, noted INTEGER DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_runs_imported ON runs(imported_at DESC);
CREATE INDEX IF NOT EXISTS idx_runs_file_period ON runs(file, period);

CREATE TABLE IF NOT EXISTS subaccounts(
  id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
  code TEXT NOT NULL, is_total INTEGER DEFAULT 0,
  opening REAL, debit REAL, credit REAL, closing REAL);

CREATE TABLE IF NOT EXISTS items(
  id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
  subaccount_id INTEGER NOT NULL REFERENCES subaccounts(id) ON DELETE CASCADE,
  row_no INTEGER NOT NULL,
  name_raw TEXT NOT NULL, name_norm TEXT,
  open_debit REAL, open_credit REAL, in_debit REAL, out_credit REAL,
  close_debit REAL, close_credit REAL,
  qty_open REAL, qty_in REAL, qty_out REAL, qty_close REAL,
  unit_price REAL, arith_ok INTEGER DEFAULT 1, has_qty INTEGER DEFAULT 1,
  qty_mismatch INTEGER DEFAULT 0,
  rule_id INTEGER REFERENCES rules(id) ON DELETE SET NULL,
  category_id INTEGER REFERENCES categories(id) ON DELETE SET NULL,
  severity INTEGER DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_items_run ON items(run_id);
CREATE INDEX IF NOT EXISTS idx_items_sub ON items(subaccount_id);
CREATE INDEX IF NOT EXISTS idx_items_flag ON items(rule_id);

CREATE TABLE IF NOT EXISTS flags(
  id INTEGER PRIMARY KEY, item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
  rule_id INTEGER NOT NULL REFERENCES rules(id) ON DELETE CASCADE,
  severity INTEGER DEFAULT 1, amount REAL, rationale TEXT);
CREATE INDEX IF NOT EXISTS idx_flags_item ON flags(item_id);
CREATE INDEX IF NOT EXISTS idx_flags_rule ON flags(rule_id);

CREATE TABLE IF NOT EXISTS notes(
  id INTEGER PRIMARY KEY, item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
  note TEXT, doc_no TEXT, doc_date TEXT, contract TEXT, purchase_date TEXT,
  author TEXT, created_at TEXT);
CREATE INDEX IF NOT EXISTS idx_notes_item ON notes(item_id);

CREATE TABLE IF NOT EXISTS categories(
  id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, color TEXT NOT NULL,
  description TEXT, sort_order INTEGER DEFAULT 0, active INTEGER DEFAULT 1);

CREATE TABLE IF NOT EXISTS rules(
  id INTEGER PRIMARY KEY, code TEXT UNIQUE NOT NULL,
  category_id INTEGER NOT NULL REFERENCES categories(id),
  type TEXT NOT NULL,
  pattern TEXT NOT NULL, severity INTEGER DEFAULT 1,
  rationale TEXT, source_doc TEXT, active INTEGER DEFAULT 1, updated_at TEXT);
CREATE INDEX IF NOT EXISTS idx_rules_category ON rules(category_id);

CREATE TABLE IF NOT EXISTS term_lists(
  id INTEGER PRIMARY KEY,
  rule_id INTEGER REFERENCES rules(id) ON DELETE CASCADE,
  name TEXT NOT NULL,
  description TEXT,
  source_doc TEXT,
  as_of_date TEXT,
  active INTEGER DEFAULT 1,
  sort_order INTEGER DEFAULT 0,
  created_at TEXT, updated_at TEXT);
CREATE UNIQUE INDEX IF NOT EXISTS idx_term_lists_rule_name ON term_lists(rule_id, name);
CREATE INDEX IF NOT EXISTS idx_term_lists_rule ON term_lists(rule_id);

CREATE TABLE IF NOT EXISTS terms(
  id INTEGER PRIMARY KEY, rule_id INTEGER REFERENCES rules(id) ON DELETE CASCADE,
  term TEXT NOT NULL, term_norm TEXT NOT NULL,
  scope TEXT DEFAULT 'local',
  origin TEXT DEFAULT 'manual',
  list_name TEXT,
  source_doc TEXT, as_of_date TEXT,
  active INTEGER DEFAULT 1, created_at TEXT, updated_at TEXT);
CREATE UNIQUE INDEX IF NOT EXISTS idx_terms_norm ON terms(term_norm, scope, COALESCE(list_name,'')) WHERE active=1;
CREATE INDEX IF NOT EXISTS idx_terms_rule ON terms(rule_id);

CREATE TABLE IF NOT EXISTS kinds(id INTEGER PRIMARY KEY, kind TEXT UNIQUE,
  shelf_life_months INTEGER, consumable INTEGER DEFAULT 0, note TEXT);

CREATE TABLE IF NOT EXISTS sources(id INTEGER PRIMARY KEY, name TEXT NOT NULL, file TEXT,
  as_of_date TEXT, imported_at TEXT, rows INTEGER, active INTEGER DEFAULT 1);

CREATE TABLE IF NOT EXISTS conflicts(
  id INTEGER PRIMARY KEY, term_norm TEXT NOT NULL,
  manual_id INTEGER REFERENCES terms(id), import_id INTEGER REFERENCES terms(id),
  resolved INTEGER DEFAULT 0, resolution TEXT, created_at TEXT);
)sql";

bool HasColumn(sqlite3* db, const char* table, const char* column) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    std::string sql = "PRAGMA table_info(";
    sql += table;
    sql += ")";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (ColumnText(st, 1) == column) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(st);
    return found;
}

bool ExecSql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Migration error: " << (err ? err : "unknown") << std::endl;
        sqlite3_free(err);
        return false;
    }
    return true;
}

// Старые базы связывали правила с категориями по имени, а термины/отметки —
// по коду правила. Перестраиваем такие таблицы в связи по id.
bool MigrateToIdRelations(sqlite3* db) {
    if (!db) return false;
    const bool legacyRules = HasColumn(db, "rules", "category");
    const bool legacyTerms = HasColumn(db, "terms", "rule_code");
    const bool legacyFlags = HasColumn(db, "flags", "rule_code");
    const bool legacyItems = HasColumn(db, "items", "rule_code");
    if (!legacyRules && !legacyTerms && !legacyFlags && !legacyItems) return true;

    sqlite3_exec(db, "PRAGMA foreign_keys=OFF", nullptr, nullptr, nullptr);
    if (!ExecSql(db, "BEGIN")) {
        sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
        return false;
    }

    auto fail = [&]() {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
        return false;
    };

    if (legacyRules) {
        if (!ExecSql(db, "ALTER TABLE rules ADD COLUMN category_id INTEGER")) return fail();
        if (!ExecSql(db, "INSERT INTO categories(name, color, description, sort_order, active) "
                         "SELECT 'Без категории', '#7f8c8d', 'Категория по умолчанию', 900, 1 "
                         "WHERE NOT EXISTS (SELECT 1 FROM categories)")) return fail();
        if (!ExecSql(db, "UPDATE rules SET category_id = "
                         "SELECT id FROM categories WHERE categories.name = rules.category")) return fail();
        if (!ExecSql(db, "UPDATE rules SET category_id = "
                         "SELECT id FROM categories ORDER BY sort_order, name LIMIT 1 "
                         "WHERE category_id IS NULL")) return fail();
        if (!ExecSql(db, "CREATE TABLE rules_new("
                         "id INTEGER PRIMARY KEY, code TEXT UNIQUE NOT NULL, "
                         "category_id INTEGER NOT NULL REFERENCES categories(id), "
                         "type TEXT NOT NULL, pattern TEXT NOT NULL, severity INTEGER DEFAULT 1, "
                         "rationale TEXT, source_doc TEXT, active INTEGER DEFAULT 1, updated_at TEXT)")) return fail();
        if (!ExecSql(db, "INSERT INTO rules_new(id, code, category_id, type, pattern, severity, "
                         "rationale, source_doc, active, updated_at) "
                         "SELECT id, COALESCE(code, 'rule-' || id), category_id, COALESCE(type, 'term'), "
                         "COALESCE(pattern, ''), severity, rationale, source_doc, active, updated_at FROM rules")) return fail();
        if (!ExecSql(db, "DROP TABLE rules")) return fail();
        if (!ExecSql(db, "ALTER TABLE rules_new RENAME TO rules")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_rules_category ON rules(category_id)")) return fail();
    }

    if (legacyTerms) {
        if (!ExecSql(db, "ALTER TABLE terms ADD COLUMN rule_id INTEGER")) return fail();
        if (!ExecSql(db, "UPDATE terms SET rule_id = SELECT id FROM rules WHERE rules.code = terms.rule_code")) return fail();
        if (!ExecSql(db, "CREATE TABLE terms_new("
                         "id INTEGER PRIMARY KEY, rule_id INTEGER REFERENCES rules(id) ON DELETE CASCADE, "
                         "term TEXT NOT NULL, term_norm TEXT NOT NULL, scope TEXT DEFAULT 'local', "
                         "origin TEXT DEFAULT 'manual', list_name TEXT, source_doc TEXT, as_of_date TEXT, "
                         "active INTEGER DEFAULT 1, created_at TEXT, updated_at TEXT)")) return fail();
        if (!ExecSql(db, "INSERT INTO terms_new(id, rule_id, term, term_norm, scope, origin, list_name, "
                         "source_doc, as_of_date, active, created_at, updated_at) "
                         "SELECT id, rule_id, term, term_norm, scope, origin, list_name, source_doc, "
                         "as_of_date, active, created_at, updated_at FROM terms")) return fail();
        if (!ExecSql(db, "DROP TABLE terms")) return fail();
        if (!ExecSql(db, "ALTER TABLE terms_new RENAME TO terms")) return fail();
        if (!ExecSql(db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_terms_norm "
                         "ON terms(term_norm, scope, COALESCE(list_name,'')) WHERE active=1")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_terms_rule ON terms(rule_id)")) return fail();
    }

    if (legacyFlags) {
        if (!ExecSql(db, "ALTER TABLE flags ADD COLUMN rule_id INTEGER")) return fail();
        if (!ExecSql(db, "UPDATE flags SET rule_id = SELECT id FROM rules WHERE rules.code = flags.rule_code")) return fail();
        if (!ExecSql(db, "DELETE FROM flags WHERE rule_id IS NULL")) return fail();
        if (!ExecSql(db, "CREATE TABLE flags_new("
                         "id INTEGER PRIMARY KEY, item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE, "
                         "rule_id INTEGER NOT NULL REFERENCES rules(id) ON DELETE CASCADE, "
                         "severity INTEGER DEFAULT 1, amount REAL, rationale TEXT)")) return fail();
        if (!ExecSql(db, "INSERT INTO flags_new(id, item_id, rule_id, severity, amount, rationale) "
                         "SELECT id, item_id, rule_id, severity, amount, rationale FROM flags")) return fail();
        if (!ExecSql(db, "DROP TABLE flags")) return fail();
        if (!ExecSql(db, "ALTER TABLE flags_new RENAME TO flags")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_flags_item ON flags(item_id)")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_flags_rule ON flags(rule_id)")) return fail();
    }

    if (legacyItems) {
        const bool legacyItemCategory = HasColumn(db, "items", "category");
        if (!ExecSql(db, "ALTER TABLE items ADD COLUMN rule_id INTEGER")) return fail();
        if (!ExecSql(db, "ALTER TABLE items ADD COLUMN category_id INTEGER")) return fail();
        if (!ExecSql(db, "UPDATE items SET rule_id = SELECT id FROM rules WHERE rules.code = items.rule_code")) return fail();
        if (legacyItemCategory) {
            if (!ExecSql(db, "UPDATE items SET category_id = SELECT id FROM categories WHERE categories.name = items.category")) return fail();
        }
        if (!ExecSql(db, "CREATE TABLE items_new("
                         "id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE, "
                         "subaccount_id INTEGER NOT NULL REFERENCES subaccounts(id) ON DELETE CASCADE, "
                         "row_no INTEGER NOT NULL, name_raw TEXT NOT NULL, name_norm TEXT, "
                         "open_debit REAL, open_credit REAL, in_debit REAL, out_credit REAL, "
                         "close_debit REAL, close_credit REAL, "
                         "qty_open REAL, qty_in REAL, qty_out REAL, qty_close REAL, unit_price REAL, "
                         "arith_ok INTEGER DEFAULT 1, has_qty INTEGER DEFAULT 1, qty_mismatch INTEGER DEFAULT 0, "
                         "rule_id INTEGER REFERENCES rules(id) ON DELETE SET NULL, "
                         "category_id INTEGER REFERENCES categories(id) ON DELETE SET NULL, "
                         "severity INTEGER DEFAULT 0)")) return fail();
        if (!ExecSql(db, "INSERT INTO items_new(id, run_id, subaccount_id, row_no, name_raw, name_norm, "
                         "open_debit, open_credit, in_debit, out_credit, close_debit, close_credit, "
                         "qty_open, qty_in, qty_out, qty_close, unit_price, arith_ok, has_qty, qty_mismatch, "
                         "rule_id, category_id, severity) "
                         "SELECT id, run_id, subaccount_id, row_no, name_raw, name_norm, "
                         "open_debit, open_credit, in_debit, out_credit, close_debit, close_credit, "
                         "qty_open, qty_in, qty_out, qty_close, unit_price, arith_ok, has_qty, qty_mismatch, "
                         "rule_id, category_id, severity FROM items")) return fail();
        if (!ExecSql(db, "DROP TABLE items")) return fail();
        if (!ExecSql(db, "ALTER TABLE items_new RENAME TO items")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_items_run ON items(run_id)")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_items_sub ON items(subaccount_id)")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_items_flag ON items(rule_id)")) return fail();
    }

    if (!ExecSql(db, "COMMIT")) return fail();
    sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
    return true;
}

// Старый справочник «видов перечней» (list_kinds) заменяется справочником
// самих перечней (term_lists). Перечни наследуют источник и дату от терминов.
bool MigrateTermLists(sqlite3* db) {
    if (!db) return false;
    const bool legacyKind = HasColumn(db, "terms", "list_kind_id");
    const bool hasListKinds = HasColumn(db, "list_kinds", "name");
    if (!legacyKind && !hasListKinds) return true;

    sqlite3_exec(db, "PRAGMA foreign_keys=OFF", nullptr, nullptr, nullptr);
    if (!ExecSql(db, "BEGIN")) {
        sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
        return false;
    }

    auto fail = [&]() {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
        return false;
    };

    if (legacyKind) {
        if (!ExecSql(db, "INSERT OR IGNORE INTO term_lists(rule_id, name, source_doc, as_of_date, "
                         "active, created_at, updated_at) "
                         "SELECT rule_id, COALESCE(list_name,''), MIN(source_doc), MIN(as_of_date), 1, "
                         "COALESCE(MIN(created_at),''), COALESCE(MAX(updated_at),'') "
                         "FROM terms GROUP BY rule_id, COALESCE(list_name,'')")) return fail();
        if (!ExecSql(db, "UPDATE term_lists SET "
                         "source_doc = COALESCE(NULLIF(source_doc,''), "
                         "  (SELECT MIN(t.source_doc) FROM terms t "
                         "   WHERE t.rule_id IS term_lists.rule_id AND COALESCE(t.list_name,'') = term_lists.name)), "
                         "as_of_date = COALESCE(NULLIF(as_of_date,''), "
                         "  (SELECT MIN(t.as_of_date) FROM terms t "
                         "   WHERE t.rule_id IS term_lists.rule_id AND COALESCE(t.list_name,'') = term_lists.name))")) return fail();
        if (!ExecSql(db, "CREATE TABLE terms_new("
                         "id INTEGER PRIMARY KEY, rule_id INTEGER REFERENCES rules(id) ON DELETE CASCADE, "
                         "term TEXT NOT NULL, term_norm TEXT NOT NULL, scope TEXT DEFAULT 'local', "
                         "origin TEXT DEFAULT 'manual', list_name TEXT, source_doc TEXT, as_of_date TEXT, "
                         "active INTEGER DEFAULT 1, created_at TEXT, updated_at TEXT)")) return fail();
        if (!ExecSql(db, "INSERT INTO terms_new(id, rule_id, term, term_norm, scope, origin, list_name, "
                         "source_doc, as_of_date, active, created_at, updated_at) "
                         "SELECT id, rule_id, term, term_norm, scope, origin, list_name, source_doc, "
                         "as_of_date, active, created_at, updated_at FROM terms")) return fail();
        if (!ExecSql(db, "DROP TABLE terms")) return fail();
        if (!ExecSql(db, "ALTER TABLE terms_new RENAME TO terms")) return fail();
        if (!ExecSql(db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_terms_norm "
                         "ON terms(term_norm, scope, COALESCE(list_name,'')) WHERE active=1")) return fail();
        if (!ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_terms_rule ON terms(rule_id)")) return fail();
    }

    if (hasListKinds) {
        if (!ExecSql(db, "DROP TABLE list_kinds")) return fail();
    }
    if (!ExecSql(db, "DROP INDEX IF EXISTS idx_terms_kind")) return fail();

    if (!ExecSql(db, "COMMIT")) return fail();
    sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
    return true;
}

}  // namespace

Database::~Database() {
    Close();
}

bool Database::Open(const std::string& target) {
    Close();
    std::filesystem::create_directories(std::filesystem::path(target).parent_path());
    if (sqlite3_open(target.c_str(), &db) != SQLITE_OK) {
        if (db) {
            std::cerr << "SQLite open error: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_close(db);
            db = nullptr;
        }
        return false;
    }
    path = target;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    if (!EnsureSchema()) return false;
    SeedDefaults();
    return true;
}

void Database::Close() {
    if (db) {
        if (txnDepth > 0) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            txnDepth = 0;
        }
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::EnsureSchema() {
    char* err = nullptr;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Schema error: " << (err ? err : "unknown") << std::endl;
        sqlite3_free(err);
        return false;
    }

    if (!MigrateToIdRelations(db)) return false;
    if (!MigrateTermLists(db)) return false;

    // Один и тот же термин может жить в нескольких перечнях одного scope,
    // поэтому уникальный индекс стал мягче: (term_norm, scope, list_name).
    bool listAwareIndex = false;
    if (sqlite3_prepare_v2(db,
            "SELECT sql FROM sqlite_master WHERE type='index' AND name='idx_terms_norm'",
            -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            listAwareIndex = ColumnText(st, 0).find("list_name") != std::string::npos;
        }
        sqlite3_finalize(st);
    }
    if (!listAwareIndex) {
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_terms_norm", nullptr, nullptr, nullptr);
        if (sqlite3_exec(db,
                "CREATE UNIQUE INDEX IF NOT EXISTS idx_terms_norm "
                "ON terms(term_norm, scope, COALESCE(list_name,'')) WHERE active=1",
                nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "Migration error: " << (err ? err : "unknown") << std::endl;
            sqlite3_free(err);
            return false;
        }
    }

    bool hasHasQty = false;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(items)", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (ColumnText(st, 1) == "has_qty") {
                hasHasQty = true;
                break;
            }
        }
        sqlite3_finalize(st);
    }
    if (!hasHasQty) {
        if (sqlite3_exec(db, "ALTER TABLE items ADD COLUMN has_qty INTEGER DEFAULT 1",
                         nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "Migration error: " << (err ? err : "unknown") << std::endl;
            sqlite3_free(err);
            return false;
        }
    }

    bool hasQtyMismatch = false;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(items)", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (ColumnText(st, 1) == "qty_mismatch") {
                hasQtyMismatch = true;
                break;
            }
        }
        sqlite3_finalize(st);
    }
    if (!hasQtyMismatch) {
        if (sqlite3_exec(db, "ALTER TABLE items ADD COLUMN qty_mismatch INTEGER DEFAULT 0",
                         nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "Migration error: " << (err ? err : "unknown") << std::endl;
            sqlite3_free(err);
            return false;
        }
    }

    return true;
}

bool Database::EnsureSeedExamples() {
    if (!db) return false;
    if (!Begin()) return false;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO categories(name, color, description, sort_order) VALUES(?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    for (const auto& c : kSeedCategories) {
        sqlite3_bind_text(st, 1, c.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, c.color, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, c.description, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, c.sortOrder);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    int64_t noTurnoverCategoryId = 0;
    if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name = ?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_bind_text(st, 1, "Без оборота", -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) noTurnoverCategoryId = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);

    if (noTurnoverCategoryId != 0) {
        if (sqlite3_prepare_v2(db,
                "UPDATE rules SET category_id = ? "
                "WHERE code = 'S05' AND pattern LIKE 'no_turnover%' AND category_id <> ?",
                -1, &st, nullptr) != SQLITE_OK) {
            Rollback();
            return false;
        }
        sqlite3_bind_int64(st, 1, noTurnoverCategoryId);
        sqlite3_bind_int64(st, 2, noTurnoverCategoryId);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    if (sqlite3_prepare_v2(db,
            "DELETE FROM rules WHERE code = 'SUB02' AND type = 'synonym' "
            "AND pattern LIKE 'sub:105.31;перчатки%'",
            -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_step(st);
    sqlite3_finalize(st);

    return Commit();
}

bool Database::SeedDefaults() {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM rules", -1, &st, nullptr) != SQLITE_OK)
        return false;
    int ruleCount = 0;
    if (sqlite3_step(st) == SQLITE_ROW) ruleCount = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (ruleCount > 0) return EnsureSeedExamples();

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM categories", -1, &st, nullptr) != SQLITE_OK)
        return false;
    int categoryCount = 0;
    if (sqlite3_step(st) == SQLITE_ROW) categoryCount = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (categoryCount > 0) return true;

    return SeedExampleRules();
}

bool Database::SeedDefaultRules() {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM rules", -1, &st, nullptr) != SQLITE_OK)
        return false;
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (count > 0) return true;
    return SeedExampleRules();
}

bool Database::SeedExampleRules() {
    if (!db) return false;
    if (!Begin()) return false;

    std::unordered_map<std::string, int64_t> categoryIds;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO categories(name, color, description, sort_order) VALUES(?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    for (const auto& c : kSeedCategories) {
        sqlite3_bind_text(st, 1, c.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, c.color, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, c.description, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, c.sortOrder);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db, "SELECT id, name FROM categories", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        categoryIds[ColumnText(st, 1)] = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO rules(code, category_id, type, pattern, severity, rationale, source_doc, active, updated_at) "
            "VALUES(?,?,?,?,?,?,?,1,?)", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    for (const auto& r : kSeedRules) {
        auto it = categoryIds.find(r.category);
        if (it == categoryIds.end()) continue;
        sqlite3_bind_text(st, 1, r.code, -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 2, it->second);
        sqlite3_bind_text(st, 3, r.type, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 4, r.pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 5, r.severity);
        sqlite3_bind_text(st, 6, r.rationale, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 7, r.sourceDoc, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 8, NowIso().c_str(), -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    std::unordered_map<std::string, int64_t> ruleIds;
    if (sqlite3_prepare_v2(db, "SELECT id, code FROM rules", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        ruleIds[ColumnText(st, 1)] = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db,
            "SELECT COUNT(*) FROM terms WHERE rule_id=? AND term_norm=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_stmt* insertTerm = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO terms(rule_id, term, term_norm, scope, origin, list_name, source_doc, active, created_at) "
            "VALUES(?,?,?,?,?,?,?,1,?)", -1, &insertTerm, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    for (const auto& t : kSeedTerms) {
        auto it = ruleIds.find(t.ruleCode);
        if (it == ruleIds.end()) continue;
        std::string norm = ToLowerUtf8(TrimUtf8(t.term));
        sqlite3_bind_text(st, 1, norm.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 2, it->second);
        bool exists = false;
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) > 0) exists = true;
        sqlite3_reset(st);
        if (exists) continue;

        sqlite3_bind_int64(insertTerm, 1, it->second);
        sqlite3_bind_text(insertTerm, 2, t.term, -1, SQLITE_STATIC);
        sqlite3_bind_text(insertTerm, 3, norm.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertTerm, 4, t.scope, -1, SQLITE_STATIC);
        sqlite3_bind_text(insertTerm, 5, t.origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(insertTerm, 6, t.listName, -1, SQLITE_STATIC);
        sqlite3_bind_text(insertTerm, 7, t.sourceDoc, -1, SQLITE_STATIC);
        sqlite3_bind_text(insertTerm, 8, NowIso().c_str(), -1, SQLITE_STATIC);
        sqlite3_step(insertTerm);
        sqlite3_reset(insertTerm);
    }
    sqlite3_finalize(st);
    sqlite3_finalize(insertTerm);

    return Commit();
}

int Database::CountRulesByCodePrefix(const std::string& prefix) {
    if (!db) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM rules WHERE code LIKE ?", -1, &st, nullptr) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, (prefix + "%").c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

bool Database::Begin() {
    if (!db) return false;
    if (txnDepth > 0) {
        ++txnDepth;
        return true;
    }
    if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    txnDepth = 1;
    return true;
}

bool Database::Commit() {
    if (!db) return false;
    if (txnDepth == 0) return true;
    if (--txnDepth > 0) return true;
    return sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Database::Rollback() {
    if (!db) return false;
    if (txnDepth == 0) return true;
    if (--txnDepth > 0) return true;
    return sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

int64_t Database::LastInsertId() const { return sqlite3_last_insert_rowid(db); }

bool Database::Backup(const std::string& reason, std::string& outPath) {
    if (!db) return false;
    std::filesystem::create_directories(AppDataDir() + "/backups");
    outPath = AppDataDir() + "/backups/audit105_" + NowCompact() + "_" + reason + ".db";

    sqlite3* dest = nullptr;
    if (sqlite3_open(outPath.c_str(), &dest) != SQLITE_OK) {
        if (dest) sqlite3_close(dest);
        return false;
    }
    sqlite3_backup* bk = sqlite3_backup_init(dest, "main", db, "main");
    bool ok = bk && sqlite3_backup_step(bk, -1) == SQLITE_DONE;
    if (bk) sqlite3_backup_finish(bk);
    sqlite3_close(dest);
    return ok;
}

bool Database::SaveAs(const std::string& target) {
    if (!db) return false;
    std::filesystem::create_directories(std::filesystem::path(target).parent_path());
    sqlite3* dest = nullptr;
    if (sqlite3_open(target.c_str(), &dest) != SQLITE_OK) {
        if (dest) sqlite3_close(dest);
        return false;
    }
    sqlite3_backup* bk = sqlite3_backup_init(dest, "main", db, "main");
    bool ok = bk && sqlite3_backup_step(bk, -1) == SQLITE_DONE;
    if (bk) sqlite3_backup_finish(bk);
    sqlite3_close(dest);
    if (ok) path = target;
    return ok;
}

std::string Database::IntegrityReport() {
    if (!db) return "База не открыта";
    std::string report;
    char* err = nullptr;
    sqlite3_stmt* st = nullptr;

    auto count = [&](const char* table) -> long long {
        std::string q = "SELECT COUNT(*) FROM " + std::string(table);
        if (sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr) != SQLITE_OK) return 0;
        long long c = 0;
        if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return c;
    };

    report += "Прогонов: " + std::to_string(count("runs")) + "\n";
    report += "Субсчетов: " + std::to_string(count("subaccounts")) + "\n";
    report += "Позиций: " + std::to_string(count("items")) + "\n";
    report += "Отметок: " + std::to_string(count("flags")) + "\n";
    report += "Примечаний: " + std::to_string(count("notes")) + "\n";
    report += "Категорий: " + std::to_string(count("categories")) + "\n";
    report += "Правил: " + std::to_string(count("rules")) + "\n";
    report += "Перечней: " + std::to_string(count("term_lists")) + "\n";
    report += "Терминов: " + std::to_string(count("terms")) + "\n";

    auto scalar = [&](const std::string& query) -> long long {
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &st, nullptr) != SQLITE_OK) return 0;
        long long value = 0;
        if (sqlite3_step(st) == SQLITE_ROW) value = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return value;
    };

    report += "Позиций без «Кол.»: " + std::to_string(scalar("SELECT COUNT(*) FROM items WHERE has_qty=0")) + "\n";
    report += "Позиций с арифметическим сбоем: " + std::to_string(scalar("SELECT COUNT(*) FROM items WHERE arith_ok=0")) + "\n";
    report += "Позиций с отрицательным остатком: " +
              std::to_string(scalar("SELECT COUNT(*) FROM items WHERE close_debit - close_credit < 0")) + "\n";
    report += "Позиций без субсчёта: " +
              std::to_string(scalar("SELECT COUNT(*) FROM items i JOIN subaccounts s ON s.id=i.subaccount_id "
                                    "WHERE s.code='' OR s.code IS NULL")) + "\n";

    int mismatches = 0;
    if (sqlite3_prepare_v2(db,
            "SELECT s.code, s.opening, s.debit, s.credit, s.closing, "
            "COALESCE(SUM(i.open_debit - i.open_credit),0), COALESCE(SUM(i.in_debit),0), "
            "COALESCE(SUM(i.out_credit),0), COALESCE(SUM(i.close_debit - i.close_credit),0) "
            "FROM subaccounts s LEFT JOIN items i ON i.subaccount_id = s.id "
            "GROUP BY s.id", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            double opening = sqlite3_column_double(st, 1);
            double debit = sqlite3_column_double(st, 2);
            double credit = sqlite3_column_double(st, 3);
            double closing = sqlite3_column_double(st, 4);
            double sumOpening = sqlite3_column_double(st, 5);
            double sumDebit = sqlite3_column_double(st, 6);
            double sumCredit = sqlite3_column_double(st, 7);
            double sumClosing = sqlite3_column_double(st, 8);
            if (std::fabs(opening - sumOpening) > 0.01 ||
                std::fabs(debit - sumDebit) > 0.01 ||
                std::fabs(credit - sumCredit) > 0.01 ||
                std::fabs(closing - sumClosing) > 0.01) {
                ++mismatches;
            }
        }
        sqlite3_finalize(st);
    }
    int fkIssues = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) ++fkIssues;
        sqlite3_finalize(st);
    }
    report += "Субсчётов, где подитог != сумма позиций: " + std::to_string(mismatches) + "\n";
    report += "Нарушений FK: " + std::to_string(fkIssues);
    (void)err;
    return report;
}

int64_t Database::InsertRun(const Run& run) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO runs(file, format, organization, period, note, account, kfo, imported_at, items, flagged, noted) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, run.file.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, run.format.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, run.organization.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, run.period.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, run.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, run.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, run.kfo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, run.importedAt.empty() ? NowIso().c_str() : run.importedAt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 9, run.items);
    sqlite3_bind_int(st, 10, run.flagged);
    sqlite3_bind_int(st, 11, run.noted);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(db);
}

int64_t Database::InsertSubaccount(int64_t runId, const Subaccount& sub) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO subaccounts(run_id, code, is_total, opening, debit, credit, closing) VALUES(?,?,?,?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(st, 1, runId);
    sqlite3_bind_text(st, 2, sub.code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, sub.isTotal ? 1 : 0);
    sqlite3_bind_double(st, 4, sub.opening);
    sqlite3_bind_double(st, 5, sub.debit);
    sqlite3_bind_double(st, 6, sub.credit);
    sqlite3_bind_double(st, 7, sub.closing);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(db);
}

int64_t Database::InsertItem(int64_t runId, int64_t subaccountId, const Item& item) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO items(run_id, subaccount_id, row_no, name_raw, name_norm, "
            "open_debit, open_credit, in_debit, out_credit, close_debit, close_credit, "
            "qty_open, qty_in, qty_out, qty_close, unit_price, arith_ok, has_qty, qty_mismatch) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(st, 1, runId);
    sqlite3_bind_int64(st, 2, subaccountId);
    sqlite3_bind_int(st, 3, item.rowNo);
    sqlite3_bind_text(st, 4, item.nameRaw.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, item.nameNorm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 6, item.openDebit);
    sqlite3_bind_double(st, 7, item.openCredit);
    sqlite3_bind_double(st, 8, item.inDebit);
    sqlite3_bind_double(st, 9, item.outCredit);
    sqlite3_bind_double(st, 10, item.closeDebit);
    sqlite3_bind_double(st, 11, item.closeCredit);
    sqlite3_bind_double(st, 12, item.qtyOpen);
    sqlite3_bind_double(st, 13, item.qtyIn);
    sqlite3_bind_double(st, 14, item.qtyOut);
    sqlite3_bind_double(st, 15, item.qtyClose);
    sqlite3_bind_double(st, 16, item.unitPrice);
    sqlite3_bind_int(st, 17, item.arithOk ? 1 : 0);
    sqlite3_bind_int(st, 18, item.hasQtyRow ? 1 : 0);
    sqlite3_bind_int(st, 19, item.qtyMismatch ? 1 : 0);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_last_insert_rowid(db);
}

std::vector<Subaccount> Database::ListSubaccounts(int64_t runId) {
    std::vector<Subaccount> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT code, is_total, opening, debit, credit, closing FROM subaccounts "
            "WHERE run_id=? ORDER BY code", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, runId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Subaccount sub;
        sub.code = ColumnText(st, 0);
        sub.isTotal = sqlite3_column_int(st, 1) != 0;
        sub.opening = sqlite3_column_double(st, 2);
        sub.debit = sqlite3_column_double(st, 3);
        sub.credit = sqlite3_column_double(st, 4);
        sub.closing = sqlite3_column_double(st, 5);
        out.push_back(std::move(sub));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<ItemRecord> Database::ListRunItems(int64_t runId) {
    std::vector<ItemRecord> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT i.id, i.row_no, i.name_raw, i.name_norm, s.code, "
            "i.open_debit, i.open_credit, i.in_debit, i.out_credit, i.close_debit, i.close_credit, "
            "i.qty_open, i.qty_in, i.qty_out, i.qty_close, i.unit_price, i.arith_ok, "
            "i.rule_id, COALESCE(r.code,''), COALESCE(c.name,''), i.severity, i.has_qty, i.qty_mismatch "
            "FROM items i JOIN subaccounts s ON s.id = i.subaccount_id "
            "LEFT JOIN rules r ON r.id = i.rule_id "
            "LEFT JOIN categories c ON c.id = i.category_id "
            "WHERE i.run_id = ? ORDER BY i.row_no", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, runId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        ItemRecord rec;
        rec.id = sqlite3_column_int64(st, 0);
        rec.item.rowNo = sqlite3_column_int(st, 1);
        rec.item.nameRaw = ColumnText(st, 2);
        rec.item.nameNorm = ColumnText(st, 3);
        rec.subaccount = ColumnText(st, 4);
        rec.item.openDebit = sqlite3_column_double(st, 5);
        rec.item.openCredit = sqlite3_column_double(st, 6);
        rec.item.inDebit = sqlite3_column_double(st, 7);
        rec.item.outCredit = sqlite3_column_double(st, 8);
        rec.item.closeDebit = sqlite3_column_double(st, 9);
        rec.item.closeCredit = sqlite3_column_double(st, 10);
        rec.item.qtyOpen = sqlite3_column_double(st, 11);
        rec.item.qtyIn = sqlite3_column_double(st, 12);
        rec.item.qtyOut = sqlite3_column_double(st, 13);
        rec.item.qtyClose = sqlite3_column_double(st, 14);
        rec.item.unitPrice = sqlite3_column_double(st, 15);
        rec.item.arithOk = sqlite3_column_int(st, 16) != 0;
        rec.ruleId = sqlite3_column_int64(st, 17);
        rec.ruleCode = ColumnText(st, 18);
        rec.category = ColumnText(st, 19);
        rec.severity = sqlite3_column_int(st, 20);
        rec.item.subaccount = ColumnText(st, 4);
        rec.item.hasQtyRow = sqlite3_column_int(st, 21) != 0;
        rec.item.qtyMismatch = sqlite3_column_int(st, 22) != 0;
        out.push_back(std::move(rec));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::InsertFlag(int64_t itemId, int64_t ruleId, int severity, double amount,
                          const std::string& rationale) {
    if (!db || ruleId == 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO flags(item_id, rule_id, severity, amount, rationale) VALUES(?,?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, itemId);
    sqlite3_bind_int64(st, 2, ruleId);
    sqlite3_bind_int(st, 3, severity);
    sqlite3_bind_double(st, 4, amount);
    sqlite3_bind_text(st, 5, rationale.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Flag> Database::ListRunFlags(int64_t runId) {
    std::vector<Flag> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT f.id, f.item_id, f.rule_id, COALESCE(r.code,''), COALESCE(c.name,''), "
            "f.severity, f.amount, f.rationale "
            "FROM flags f JOIN items i ON i.id = f.item_id "
            "JOIN rules r ON r.id = f.rule_id "
            "LEFT JOIN categories c ON c.id = r.category_id "
            "WHERE i.run_id = ? ORDER BY f.item_id, f.severity DESC", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, runId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Flag flag;
        flag.id = sqlite3_column_int64(st, 0);
        flag.itemId = sqlite3_column_int64(st, 1);
        flag.ruleId = sqlite3_column_int64(st, 2);
        flag.ruleCode = ColumnText(st, 3);
        flag.category = ColumnText(st, 4);
        flag.severity = sqlite3_column_int(st, 5);
        flag.amount = sqlite3_column_double(st, 6);
        flag.rationale = ColumnText(st, 7);
        out.push_back(std::move(flag));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<Flag> Database::ListItemFlags(int64_t itemId) {
    std::vector<Flag> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT f.id, f.item_id, f.rule_id, COALESCE(r.code,''), COALESCE(c.name,''), "
            "f.severity, f.amount, f.rationale "
            "FROM flags f JOIN rules r ON r.id = f.rule_id "
            "LEFT JOIN categories c ON c.id = r.category_id "
            "WHERE f.item_id = ? ORDER BY f.severity DESC, r.code", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, itemId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Flag flag;
        flag.id = sqlite3_column_int64(st, 0);
        flag.itemId = sqlite3_column_int64(st, 1);
        flag.ruleId = sqlite3_column_int64(st, 2);
        flag.ruleCode = ColumnText(st, 3);
        flag.category = ColumnText(st, 4);
        flag.severity = sqlite3_column_int(st, 5);
        flag.amount = sqlite3_column_double(st, 6);
        flag.rationale = ColumnText(st, 7);
        out.push_back(std::move(flag));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<Note> Database::ListNotes(int64_t itemId) {
    std::vector<Note> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id, item_id, note, doc_no, doc_date, contract, purchase_date, author, created_at "
            "FROM notes WHERE item_id = ? ORDER BY id", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, itemId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Note note;
        note.id = sqlite3_column_int64(st, 0);
        note.itemId = sqlite3_column_int64(st, 1);
        note.note = ColumnText(st, 2);
        note.docNo = ColumnText(st, 3);
        note.docDate = ColumnText(st, 4);
        note.contract = ColumnText(st, 5);
        note.purchaseDate = ColumnText(st, 6);
        note.author = ColumnText(st, 7);
        note.createdAt = ColumnText(st, 8);
        out.push_back(std::move(note));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::AddNote(const Note& note) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO notes(item_id, note, doc_no, doc_date, contract, purchase_date, author, created_at) "
            "VALUES(?,?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, note.itemId);
    sqlite3_bind_text(st, 2, note.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, note.docNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, note.docDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, note.contract.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, note.purchaseDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, note.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, NowIso().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateNote(const Note& note) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "UPDATE notes SET note=?, doc_no=?, doc_date=?, contract=?, purchase_date=?, author=? WHERE id=?",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, note.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, note.docNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, note.docDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, note.contract.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, note.purchaseDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, note.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 7, note.id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::DeleteNote(int64_t id) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM notes WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

int Database::CountRunNotes(int64_t runId) {
    if (!db) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT COUNT(*) FROM notes n JOIN items i ON i.id = n.item_id WHERE i.run_id = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(st, 1, runId);
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

int64_t Database::ImportRun(const Run& run, const ParseReport& report) {
    auto fail = [&](const char* where) -> int64_t {
        std::cerr << "ImportRun failed at " << where << ": " << sqlite3_errmsg(db) << std::endl;
        Rollback();
        return 0;
    };

    if (!Begin()) return fail("BEGIN");
    int64_t runId = InsertRun(run);
    if (runId == 0) return fail("runs insert");

    std::unordered_map<std::string, int64_t> subIds;
    for (const Subaccount& sub : report.subaccounts) {
        int64_t subId = InsertSubaccount(runId, sub);
        if (subId == 0) return fail("subaccounts insert");
        subIds[sub.code] = subId;
    }

    for (const Item& item : report.items) {
        auto it = subIds.find(item.subaccount);
        if (it == subIds.end()) {
            Subaccount orphan;
            orphan.code = item.subaccount;
            orphan.isTotal = false;
            int64_t orphanId = InsertSubaccount(runId, orphan);
            if (orphanId == 0) return fail("orphan subaccount insert");
            it = subIds.find(item.subaccount);
        }

        int64_t itemId = InsertItem(runId, it->second, item);
        if (itemId == 0) return fail("items insert");
        (void)itemId;
    }

    int flagged = ApplyRulesInternal(runId);
    if (flagged < 0) return fail("rules apply");

    if (!UpdateRunStats(runId, static_cast<int>(report.items.size()), flagged, 0)) {
        return fail("run stats update");
    }
    if (!Commit()) return fail("COMMIT");
    return runId;
}

static void BindLike(sqlite3_stmt* st, int pos, const std::string& value) {
    sqlite3_bind_text(st, pos, ("%" + value + "%").c_str(), -1, SQLITE_TRANSIENT);
}

std::vector<Run> Database::ListRuns(const std::string& organization,
                                    const std::string& period,
                                    const std::string& note,
                                    bool recentOnly,
                                    int limit) {
    std::string sql = "SELECT id, file, format, organization, period, note, account, kfo, imported_at, items, flagged, noted FROM runs WHERE 1=1";
    if (!organization.empty()) sql += " AND organization LIKE ?";
    if (!period.empty()) sql += " AND period LIKE ?";
    if (!note.empty()) sql += " AND note LIKE ?";
    if (recentOnly) sql += " AND imported_at >= datetime('now', '-30 days')";
    sql += " ORDER BY imported_at DESC LIMIT ?";

    std::vector<Run> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    int pos = 1;
    if (!organization.empty()) BindLike(st, pos++, organization);
    if (!period.empty()) BindLike(st, pos++, period);
    if (!note.empty()) BindLike(st, pos++, note);
    sqlite3_bind_int(st, pos, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Run r;
        r.id = sqlite3_column_int64(st, 0);
        r.file = (const char*)sqlite3_column_text(st, 1);
        r.format = (const char*)sqlite3_column_text(st, 2);
        r.organization = (const char*)sqlite3_column_text(st, 3);
        r.period = (const char*)sqlite3_column_text(st, 4);
        r.note = (const char*)sqlite3_column_text(st, 5);
        r.account = (const char*)sqlite3_column_text(st, 6);
        r.kfo = (const char*)sqlite3_column_text(st, 7);
        r.importedAt = (const char*)sqlite3_column_text(st, 8);
        r.items = sqlite3_column_int(st, 9);
        r.flagged = sqlite3_column_int(st, 10);
        r.noted = sqlite3_column_int(st, 11);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::GetRun(int64_t id, Run& out) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT id, file, format, organization, period, note, account, kfo, imported_at, items, flagged, noted "
        "FROM runs WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(st, 0);
        out.file = (const char*)sqlite3_column_text(st, 1);
        out.format = (const char*)sqlite3_column_text(st, 2);
        out.organization = (const char*)sqlite3_column_text(st, 3);
        out.period = (const char*)sqlite3_column_text(st, 4);
        out.note = (const char*)sqlite3_column_text(st, 5);
        out.account = (const char*)sqlite3_column_text(st, 6);
        out.kfo = (const char*)sqlite3_column_text(st, 7);
        out.importedAt = (const char*)sqlite3_column_text(st, 8);
        out.items = sqlite3_column_int(st, 9);
        out.flagged = sqlite3_column_int(st, 10);
        out.noted = sqlite3_column_int(st, 11);
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

int Database::CountRunsByFile(const std::string& file) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM runs WHERE file = ?", -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(st, 1, file.c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

bool Database::DeleteRun(int64_t id, int& removedItems, double& removedSum) {
    sqlite3_stmt* st = nullptr;
    removedItems = 0;
    removedSum = 0.0;
    sqlite3_prepare_v2(db,
        "SELECT COUNT(*), COALESCE(SUM(close_debit - close_credit), 0) FROM items WHERE run_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        removedItems = sqlite3_column_int(st, 0);
        removedSum = sqlite3_column_double(st, 1);
    }
    sqlite3_finalize(st);

    sqlite3_prepare_v2(db, "DELETE FROM runs WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db) > 0;
}

bool Database::UpdateRunStats(int64_t id, int items, int flagged, int noted) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "UPDATE runs SET items=?, flagged=?, noted=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, items);
    sqlite3_bind_int(st, 2, flagged);
    sqlite3_bind_int(st, 3, noted);
    sqlite3_bind_int64(st, 4, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db) > 0;
}

std::vector<Category> Database::ListCategories() {
    std::vector<Category> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT id, name, color, description, sort_order, active FROM categories ORDER BY sort_order, name",
        -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Category c;
        c.id = sqlite3_column_int64(st, 0);
        c.name = (const char*)sqlite3_column_text(st, 1);
        c.color = (const char*)sqlite3_column_text(st, 2);
        c.description = (const char*)sqlite3_column_text(st, 3);
        c.sortOrder = sqlite3_column_int(st, 4);
        c.active = sqlite3_column_int(st, 5) != 0;
        out.push_back(std::move(c));
    }
    sqlite3_finalize(st);
    return out;
}

static bool CategoryExists(sqlite3* db, int64_t id) {
    if (!db || id == 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM categories WHERE id=? LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return found;
}

static int64_t FallbackCategoryId(sqlite3* db, int64_t excludeId = 0) {
    if (!db) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id FROM categories WHERE active=1 AND id<>? ORDER BY sort_order, name LIMIT 1",
            -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, excludeId);
        if (sqlite3_step(st) == SQLITE_ROW) {
            int64_t id = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
            return id;
        }
        sqlite3_finalize(st);
    }

    const std::string name = "Черновик";
    if (sqlite3_prepare_v2(db,
            "INSERT INTO categories(name, color, description, sort_order, active) VALUES(?,?,?,?,1)",
            -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, "#7f8c8d", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, "Временная категория для новых правил", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, 900);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (ok) return sqlite3_last_insert_rowid(db);

    if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name=? LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int64_t existing = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return existing;
}

static std::string UniqueRuleCode(sqlite3* db, const std::string& base) {
    std::string code = base.empty() ? "новый-код" : base;
    for (int suffix = 2; suffix <= 999; ++suffix) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM rules WHERE code=? LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
            return code;
        }
        sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
        bool taken = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        if (!taken) return code;
        code = base + "-" + std::to_string(suffix);
    }
    return code;
}

static std::string UniqueCategoryName(sqlite3* db, const std::string& base) {
    std::string name = base.empty() ? "Новая категория" : base;
    for (int suffix = 2; suffix <= 999; ++suffix) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM categories WHERE name=? LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
            return name;
        }
        sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        bool taken = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        if (!taken) return name;
        name = base + " " + std::to_string(suffix);
    }
    return name;
}

static std::string UniqueTermName(sqlite3* db, const std::string& base, const std::string& scope) {
    std::string name = base.empty() ? "новый термин" : base;
    for (int suffix = 2; suffix <= 999; ++suffix) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT 1 FROM terms WHERE term_norm=? AND COALESCE(scope,'local')=? AND active=1 LIMIT 1",
                -1, &st, nullptr) != SQLITE_OK) {
            return name;
        }
        sqlite3_bind_text(st, 1, NormalizeTextUtf8(name).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, scope.c_str(), -1, SQLITE_TRANSIENT);
        bool taken = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        if (!taken) return name;
        name = base + " " + std::to_string(suffix);
    }
    return name;
}

static std::string UniqueListName(sqlite3* db, int64_t ruleId, const std::string& base) {
    std::string name = base.empty() ? "Список" : base;
    for (int suffix = 2; suffix <= 999; ++suffix) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT 1 FROM term_lists WHERE rule_id=? AND name=? "
                "UNION SELECT 1 FROM terms WHERE COALESCE(rule_id,0)=? AND COALESCE(list_name,'')=? LIMIT 1",
                -1, &st, nullptr) != SQLITE_OK) {
            return name;
        }
        sqlite3_bind_int64(st, 1, ruleId);
        sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, ruleId);
        sqlite3_bind_text(st, 4, name.c_str(), -1, SQLITE_TRANSIENT);
        bool taken = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        if (!taken) return name;
        name = base + " " + std::to_string(suffix);
    }
    return name;
}

bool Database::AddCategory(const Category& cat) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO categories(name, color, description, sort_order, active) VALUES(?,?,?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, cat.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, cat.color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, cat.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, cat.sortOrder);
    sqlite3_bind_int(st, 5, cat.active ? 1 : 0);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateCategory(const Category& cat) {
    if (!db || cat.id == 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "UPDATE categories SET name=?, color=?, description=?, sort_order=?, active=? WHERE id=?",
        -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, cat.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, cat.color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, cat.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, cat.sortOrder);
    sqlite3_bind_int(st, 5, cat.active ? 1 : 0);
    sqlite3_bind_int64(st, 6, cat.id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::DeleteCategory(int64_t id) {
    if (!db || id == 0) return false;

    if (!Begin()) return false;

    int64_t fallback = FallbackCategoryId(db, id);
    if (fallback == 0) {
        Rollback();
        return false;
    }

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE rules SET category_id=? WHERE category_id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_bind_int64(st, 1, fallback);
    sqlite3_bind_int64(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db, "UPDATE items SET category_id=? WHERE category_id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_bind_int64(st, 1, fallback);
    sqlite3_bind_int64(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);

    if (!ok || !Commit()) {
        Rollback();
        return false;
    }
    return true;
}

std::vector<Rule> Database::ListRules() {
    std::vector<Rule> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT r.id, r.code, r.category_id, COALESCE(c.name,''), r.type, r.pattern, "
            "r.severity, r.rationale, r.source_doc, r.active "
            "FROM rules r LEFT JOIN categories c ON c.id = r.category_id "
            "ORDER BY r.code", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Rule rule;
        rule.id = sqlite3_column_int64(st, 0);
        rule.code = ColumnText(st, 1);
        rule.categoryId = sqlite3_column_int64(st, 2);
        rule.category = ColumnText(st, 3);
        rule.type = ColumnText(st, 4);
        rule.pattern = ColumnText(st, 5);
        rule.severity = sqlite3_column_int(st, 6);
        rule.rationale = ColumnText(st, 7);
        rule.sourceDoc = ColumnText(st, 8);
        rule.active = sqlite3_column_int(st, 9) != 0;
        out.push_back(std::move(rule));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::AddRule(const Rule& rule) {
    if (!db || rule.code.empty() || rule.categoryId == 0 || rule.type.empty()) return false;
    if (!CategoryExists(db, rule.categoryId)) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO rules(code, category_id, type, pattern, severity, rationale, source_doc, active, updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, rule.code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, rule.categoryId);
    sqlite3_bind_text(st, 3, rule.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, rule.pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, rule.severity);
    sqlite3_bind_text(st, 6, rule.rationale.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, rule.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, rule.active ? 1 : 0);
    sqlite3_bind_text(st, 9, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateRule(const Rule& rule) {
    if (!db || rule.id == 0 || rule.code.empty() || rule.categoryId == 0 || rule.type.empty()) return false;
    if (!CategoryExists(db, rule.categoryId)) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "UPDATE rules SET code=?, category_id=?, type=?, pattern=?, severity=?, rationale=?, "
            "source_doc=?, active=?, updated_at=? WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, rule.code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, rule.categoryId);
    sqlite3_bind_text(st, 3, rule.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, rule.pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, rule.severity);
    sqlite3_bind_text(st, 6, rule.rationale.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, rule.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, rule.active ? 1 : 0);
    sqlite3_bind_text(st, 9, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 10, rule.id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::DeleteRule(int64_t id) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM rules WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db) > 0;
}

std::vector<Term> Database::ListTerms() {
    std::vector<Term> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT t.id, t.rule_id, COALESCE(r.code,''), t.term, t.term_norm, t.scope, t.origin, "
            "t.list_name, t.source_doc, t.as_of_date, t.active "
            "FROM terms t LEFT JOIN rules r ON r.id = t.rule_id "
            "ORDER BY t.term_norm", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Term term;
        term.id = sqlite3_column_int64(st, 0);
        term.ruleId = sqlite3_column_int64(st, 1);
        term.ruleCode = ColumnText(st, 2);
        term.term = ColumnText(st, 3);
        term.termNorm = ColumnText(st, 4);
        term.scope = ColumnText(st, 5);
        term.origin = ColumnText(st, 6);
        term.listName = ColumnText(st, 7);
        term.sourceDoc = ColumnText(st, 8);
        term.asOfDate = ColumnText(st, 9);
        term.active = sqlite3_column_int(st, 10) != 0;
        out.push_back(std::move(term));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::AddTerm(const Term& term) {
    if (!db || term.term.empty()) return false;
    if (term.ruleId != 0) {
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM rules WHERE id=? LIMIT 1", -1, &check, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int64(check, 1, term.ruleId);
        bool exists = sqlite3_step(check) == SQLITE_ROW;
        sqlite3_finalize(check);
        if (!exists) return false;
    }
    Term copy = term;
    copy.termNorm = NormalizeTextUtf8(copy.term);
    if (copy.scope.empty()) copy.scope = "local";
    if (copy.origin.empty()) copy.origin = "manual";
    if (!copy.listName.empty()) EnsureTermList(copy.ruleId, copy.listName);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO terms(rule_id, term, term_norm, scope, origin, list_name, source_doc, as_of_date, active, created_at, updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    if (copy.ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, copy.ruleId);
    }
    sqlite3_bind_text(st, 2, copy.term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, copy.termNorm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, copy.scope.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, copy.origin.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, copy.listName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, copy.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, copy.asOfDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 9, copy.active ? 1 : 0);
    sqlite3_bind_text(st, 10, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateTerm(const Term& term) {
    if (!db || term.id == 0 || term.term.empty()) return false;
    if (term.ruleId != 0) {
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM rules WHERE id=? LIMIT 1", -1, &check, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int64(check, 1, term.ruleId);
        bool exists = sqlite3_step(check) == SQLITE_ROW;
        sqlite3_finalize(check);
        if (!exists) return false;
    }
    Term copy = term;
    copy.termNorm = NormalizeTextUtf8(copy.term);
    if (copy.scope.empty()) copy.scope = "local";
    if (copy.origin.empty()) copy.origin = "manual";
    if (!copy.listName.empty()) EnsureTermList(copy.ruleId, copy.listName);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "UPDATE terms SET rule_id=?, term=?, term_norm=?, scope=?, origin=?, list_name=?, source_doc=?, as_of_date=?, active=?, updated_at=? "
            "WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    if (copy.ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, copy.ruleId);
    }
    sqlite3_bind_text(st, 2, copy.term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, copy.termNorm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, copy.scope.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, copy.origin.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, copy.listName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, copy.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, copy.asOfDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 9, copy.active ? 1 : 0);
    sqlite3_bind_text(st, 10, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 11, copy.id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::DeleteTerm(int64_t id) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM terms WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db) > 0;
}

bool Database::GetTerm(int64_t id, Term& out) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT t.id, COALESCE(t.rule_id,0), COALESCE(r.code,''), t.term, t.term_norm, "
            "COALESCE(t.scope,'local'), COALESCE(t.origin,'manual'), COALESCE(t.list_name,''), "
            "COALESCE(t.source_doc,''), COALESCE(t.as_of_date,''), COALESCE(t.active,1) "
            "FROM terms t LEFT JOIN rules r ON r.id = t.rule_id WHERE t.id=?",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        out.id = sqlite3_column_int64(st, 0);
        out.ruleId = sqlite3_column_int64(st, 1);
        out.ruleCode = ColumnText(st, 2);
        out.term = ColumnText(st, 3);
        out.termNorm = ColumnText(st, 4);
        out.scope = ColumnText(st, 5);
        out.origin = ColumnText(st, 6);
        out.listName = ColumnText(st, 7);
        out.sourceDoc = ColumnText(st, 8);
        out.asOfDate = ColumnText(st, 9);
        out.active = sqlite3_column_int(st, 10) != 0;
    }
    sqlite3_finalize(st);
    return found;
}

int64_t Database::DuplicateTerm(int64_t termId) {
    if (!db || termId == 0) return 0;
    Term source;
    if (!GetTerm(termId, source)) return 0;

    Term copy = source;
    copy.id = 0;
    std::string base = source.term.empty() ? "новый термин" : source.term;
    if (base.find("копия") == std::string::npos) base += " (копия)";
    copy.term = UniqueTermName(db, base, copy.scope);
    copy.termNorm = NormalizeTextUtf8(copy.term);
    if (copy.scope.empty()) copy.scope = "local";
    if (copy.origin.empty()) copy.origin = "manual";
    if (!AddTerm(copy)) return 0;
    return LastInsertId();
}

int64_t Database::CreateBlankCategory() {
    if (!db) return 0;
    int64_t next = 1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id),0)+1 FROM categories", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) next = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    Category cat;
    cat.name = UniqueCategoryName(db, "Новая категория " + std::to_string(next));
    cat.color = "#7f8c8d";
    cat.sortOrder = static_cast<int>(next);
    cat.active = true;
    if (!AddCategory(cat)) return 0;
    return LastInsertId();
}

bool Database::GetCategory(int64_t id, Category& out) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id, name, color, description, sort_order, active FROM categories WHERE id=?",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        out.id = sqlite3_column_int64(st, 0);
        out.name = ColumnText(st, 1);
        out.color = ColumnText(st, 2);
        out.description = ColumnText(st, 3);
        out.sortOrder = sqlite3_column_int(st, 4);
        out.active = sqlite3_column_int(st, 5) != 0;
    }
    sqlite3_finalize(st);
    return found;
}

int64_t Database::DuplicateCategory(int64_t categoryId) {
    if (!db || categoryId == 0) return 0;
    Category source;
    if (!GetCategory(categoryId, source)) return 0;

    Category copy = source;
    copy.id = 0;
    std::string base = source.name.empty() ? "Новая категория" : source.name;
    if (base.find("копия") == std::string::npos) base += " (копия)";
    copy.name = UniqueCategoryName(db, base);
    if (copy.color.empty()) copy.color = "#7f8c8d";
    if (!AddCategory(copy)) return 0;
    return LastInsertId();
}


bool Database::GetRule(int64_t id, Rule& out) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT r.id, r.code, r.category_id, COALESCE(c.name,''), r.type, r.pattern, "
            "r.severity, r.rationale, r.source_doc, r.active "
            "FROM rules r LEFT JOIN categories c ON c.id = r.category_id WHERE r.id=?",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        out.id = sqlite3_column_int64(st, 0);
        out.code = ColumnText(st, 1);
        out.categoryId = sqlite3_column_int64(st, 2);
        out.category = ColumnText(st, 3);
        out.type = ColumnText(st, 4);
        out.pattern = ColumnText(st, 5);
        out.severity = sqlite3_column_int(st, 6);
        out.rationale = ColumnText(st, 7);
        out.sourceDoc = ColumnText(st, 8);
        out.active = sqlite3_column_int(st, 9) != 0;
    }
    sqlite3_finalize(st);
    return found;
}

int64_t Database::CreateBlankRule(int64_t categoryId) {
    if (!db) return 0;
    if (categoryId == 0 || !CategoryExists(db, categoryId)) {
        categoryId = FallbackCategoryId(db);
        if (categoryId == 0) return 0;
    }

    int64_t next = 1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id),0)+1 FROM rules", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) next = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }

    Rule rule;
    rule.code = UniqueRuleCode(db, "новый-" + std::to_string(next));
    rule.categoryId = categoryId;
    rule.type = "term";
    rule.pattern = "";
    rule.severity = 1;
    rule.active = true;
    if (!AddRule(rule)) return 0;
    return LastInsertId();
}

int64_t Database::DuplicateRule(int64_t ruleId) {
    if (!db || ruleId == 0) return 0;
    Rule source;
    if (!GetRule(ruleId, source)) return 0;

    Rule copy = source;
    copy.id = 0;
    if (copy.categoryId == 0 || !CategoryExists(db, copy.categoryId)) {
        copy.categoryId = FallbackCategoryId(db);
        if (copy.categoryId == 0) return 0;
    }
    if (copy.type.empty()) copy.type = "term";
    if (copy.severity < 0) copy.severity = 1;

    std::string base = source.code.empty() ? "новое-правило" : source.code;
    if (base.rfind("-копия") == std::string::npos) base += "-копия";
    copy.code = UniqueRuleCode(db, base);
    if (!AddRule(copy)) return 0;
    return LastInsertId();
}

int64_t Database::CreateBlankTerm(int64_t ruleId, const std::string& listName) {
    if (!db) return 0;
    int64_t next = 1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id),0)+1 FROM terms", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) next = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }

    Term term;
    term.ruleId = ruleId;
    term.term = UniqueTermName(db, "новый термин " + std::to_string(next), "local");
    term.scope = "local";
    term.origin = "manual";
    term.listName = listName;
    term.active = true;
    if (!AddTerm(term)) return 0;
    return LastInsertId();
}

int64_t Database::EnsureTermList(int64_t ruleId, const std::string& name) {
    if (!db) return 0;
    std::string trimmed = CollapseSpacesUtf8(TrimUtf8(name));
    if (trimmed.empty()) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id FROM term_lists WHERE rule_id=? AND name=? LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(st, 1, ruleId);
    sqlite3_bind_text(st, 2, trimmed.c_str(), -1, SQLITE_TRANSIENT);
    int64_t id = 0;
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    if (id != 0) return id;

    if (sqlite3_prepare_v2(db,
            "INSERT INTO term_lists(rule_id, name, active, created_at, updated_at) VALUES(?,?,1,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    if (ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, ruleId);
    }
    sqlite3_bind_text(st, 2, trimmed.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok ? LastInsertId() : 0;
}

bool Database::TableExists(const std::string& table) const {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, table.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return exists;
}

std::vector<TermList> Database::ListTermLists() {
    std::vector<TermList> out;
    if (!db) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT l.id, COALESCE(l.rule_id,0), COALESCE(r.code,''), l.name, COALESCE(l.description,''), "
            "COALESCE(l.source_doc,''), COALESCE(l.as_of_date,''), COALESCE(l.sort_order,0), "
            "COALESCE(l.active,1), "
            "(SELECT COUNT(*) FROM terms t WHERE t.rule_id = l.rule_id AND COALESCE(t.list_name,'') = l.name), "
            "(SELECT COUNT(*) FROM terms t WHERE t.rule_id = l.rule_id AND COALESCE(t.list_name,'') = l.name AND t.active = 1) "
            "FROM term_lists l LEFT JOIN rules r ON r.id = l.rule_id "
            "ORDER BY COALESCE(l.sort_order,0), l.name", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        TermList list;
        list.id = sqlite3_column_int64(st, 0);
        list.ruleId = sqlite3_column_int64(st, 1);
        list.ruleCode = ColumnText(st, 2);
        list.name = ColumnText(st, 3);
        list.description = ColumnText(st, 4);
        list.sourceDoc = ColumnText(st, 5);
        list.asOfDate = ColumnText(st, 6);
        list.sortOrder = sqlite3_column_int(st, 7);
        list.active = sqlite3_column_int(st, 8) != 0;
        list.termCount = sqlite3_column_int(st, 9);
        list.activeTermCount = sqlite3_column_int(st, 10);
        out.push_back(std::move(list));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::GetTermList(int64_t id, TermList& out) {
    if (!db || id == 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT l.id, COALESCE(l.rule_id,0), COALESCE(r.code,''), l.name, COALESCE(l.description,''), "
            "COALESCE(l.source_doc,''), COALESCE(l.as_of_date,''), COALESCE(l.sort_order,0), "
            "COALESCE(l.active,1), "
            "(SELECT COUNT(*) FROM terms t WHERE t.rule_id = l.rule_id AND COALESCE(t.list_name,'') = l.name), "
            "(SELECT COUNT(*) FROM terms t WHERE t.rule_id = l.rule_id AND COALESCE(t.list_name,'') = l.name AND t.active = 1) "
            "FROM term_lists l LEFT JOIN rules r ON r.id = l.rule_id WHERE l.id=?",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        out.id = sqlite3_column_int64(st, 0);
        out.ruleId = sqlite3_column_int64(st, 1);
        out.ruleCode = ColumnText(st, 2);
        out.name = ColumnText(st, 3);
        out.description = ColumnText(st, 4);
        out.sourceDoc = ColumnText(st, 5);
        out.asOfDate = ColumnText(st, 6);
        out.sortOrder = sqlite3_column_int(st, 7);
        out.active = sqlite3_column_int(st, 8) != 0;
        out.termCount = sqlite3_column_int(st, 9);
        out.activeTermCount = sqlite3_column_int(st, 10);
    }
    sqlite3_finalize(st);
    return found;
}

bool Database::AddTermList(const TermList& list) {
    if (!db || list.name.empty()) return false;
    TermList copy = list;
    copy.name = CollapseSpacesUtf8(TrimUtf8(copy.name));
    if (copy.ruleId != 0) {
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM rules WHERE id=? LIMIT 1", -1, &check, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int64(check, 1, copy.ruleId);
        bool exists = sqlite3_step(check) == SQLITE_ROW;
        sqlite3_finalize(check);
        if (!exists) return false;
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO term_lists(rule_id, name, description, source_doc, as_of_date, active, sort_order, created_at, updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    if (copy.ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, copy.ruleId);
    }
    sqlite3_bind_text(st, 2, copy.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, copy.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, copy.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, copy.asOfDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, copy.active ? 1 : 0);
    sqlite3_bind_int(st, 7, copy.sortOrder);
    sqlite3_bind_text(st, 8, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateTermList(const TermList& list) {
    if (!db || list.id == 0 || list.name.empty()) return false;
    TermList copy = list;
    copy.name = CollapseSpacesUtf8(TrimUtf8(copy.name));
    if (copy.ruleId != 0) {
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM rules WHERE id=? LIMIT 1", -1, &check, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int64(check, 1, copy.ruleId);
        bool exists = sqlite3_step(check) == SQLITE_ROW;
        sqlite3_finalize(check);
        if (!exists) return false;
    }

    TermList old;
    if (!GetTermList(copy.id, old)) return false;

    if (!Begin()) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "UPDATE term_lists SET rule_id=?, name=?, description=?, source_doc=?, as_of_date=?, active=?, "
            "sort_order=?, updated_at=? WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    if (copy.ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, copy.ruleId);
    }
    sqlite3_bind_text(st, 2, copy.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, copy.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, copy.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, copy.asOfDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, copy.active ? 1 : 0);
    sqlite3_bind_int(st, 7, copy.sortOrder);
    sqlite3_bind_text(st, 8, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 9, copy.id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    if (!ok) {
        Rollback();
        return false;
    }

    // Перечень переименован или перенесён — переносим его термины.
    if (sqlite3_prepare_v2(db,
            "UPDATE terms SET rule_id=?, list_name=?, updated_at=? "
            "WHERE rule_id=? AND COALESCE(list_name,'')=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    if (copy.ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, copy.ruleId);
    }
    sqlite3_bind_text(st, 2, copy.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    if (old.ruleId == 0) {
        sqlite3_bind_null(st, 4);
    } else {
        sqlite3_bind_int64(st, 4, old.ruleId);
    }
    sqlite3_bind_text(st, 5, old.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);

    if (!Commit()) {
        Rollback();
        return false;
    }
    return true;
}

bool Database::DeleteTermList(int64_t id) {
    if (!db || id == 0) return false;
    TermList list;
    if (!GetTermList(id, list)) return false;
    if (!Begin()) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "DELETE FROM terms WHERE rule_id=? AND COALESCE(list_name,'')=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    if (list.ruleId == 0) {
        sqlite3_bind_null(st, 1);
    } else {
        sqlite3_bind_int64(st, 1, list.ruleId);
    }
    sqlite3_bind_text(st, 2, list.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    if (sqlite3_prepare_v2(db, "DELETE FROM term_lists WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return false;
    }
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    if (!ok) {
        Rollback();
        return false;
    }
    if (!Commit()) {
        Rollback();
        return false;
    }
    return true;
}

int64_t Database::CreateBlankList(int64_t ruleId) {
    if (!db) return 0;
    int64_t next = 1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id),0)+1 FROM term_lists", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) next = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    TermList list;
    list.ruleId = ruleId;
    list.name = UniqueListName(db, ruleId, "Новый перечень " + std::to_string(next));
    list.sortOrder = static_cast<int>(next);
    list.active = true;
    if (!AddTermList(list)) return 0;
    return LastInsertId();
}

int64_t Database::DuplicateTermListById(int64_t id, int& copied) {
    copied = 0;
    if (!db || id == 0) return 0;
    TermList source;
    if (!GetTermList(id, source)) return 0;

    std::string base = source.name.empty() ? "Перечень" : source.name;
    if (base.find("копия") == std::string::npos) base += " (копия)";
    std::string newName = UniqueListName(db, source.ruleId, base);

    TermList copy = source;
    copy.id = 0;
    copy.name = newName;
    if (!AddTermList(copy)) return 0;
    int64_t newId = LastInsertId();

    if (!Begin()) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO terms(rule_id, term, term_norm, scope, origin, list_name, source_doc, as_of_date, "
            "active, created_at, updated_at) "
            "SELECT rule_id, term, term_norm, scope, origin, ?, source_doc, as_of_date, active, ?, ? "
            "FROM terms WHERE rule_id=? AND COALESCE(list_name,'')=?",
            -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    sqlite3_bind_text(st, 1, newName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    if (source.ruleId == 0) {
        sqlite3_bind_null(st, 4);
    } else {
        sqlite3_bind_int64(st, 4, source.ruleId);
    }
    sqlite3_bind_text(st, 5, source.name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_DONE) copied = sqlite3_changes(db);
    sqlite3_finalize(st);
    if (!Commit()) {
        Rollback();
        return 0;
    }
    return newId;
}

int Database::CountRuleTerms(int64_t ruleId) {
    if (!db || ruleId == 0) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM terms WHERE rule_id=? AND active=1", -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(st, 1, ruleId);
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

int Database::AddTermsToList(int64_t ruleId, const std::string& listName,
                             const std::vector<std::string>& terms,
                             const std::string& sourceDoc, const std::string& asOfDate) {
    if (!db || ruleId == 0) return 0;
    EnsureTermList(ruleId, listName);
    int added = 0;
    for (const std::string& raw : terms) {
        std::string term = CollapseSpacesUtf8(TrimUtf8(raw));
        if (term.empty()) continue;
        std::string norm = NormalizeTextUtf8(term);
        std::string scope = "local";
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT id FROM terms WHERE term_norm=? AND scope=? AND COALESCE(list_name,'')=? AND rule_id=?",
                -1, &check, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(check, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(check, 2, scope.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(check, 3, listName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(check, 4, ruleId);
            bool exists = sqlite3_step(check) == SQLITE_ROW;
            sqlite3_finalize(check);
            if (exists) continue;
        }
        Term termRec;
        termRec.ruleId = ruleId;
        termRec.term = term;
        termRec.scope = scope;
        termRec.origin = "manual";
        termRec.listName = listName;
        termRec.sourceDoc = sourceDoc;
        termRec.asOfDate = asOfDate;
        termRec.active = true;
        if (AddTerm(termRec)) ++added;
    }
    return added;
}

std::string Database::DuplicateTermList(int64_t ruleId, const std::string& listName,
                                        int& copied) {
    copied = 0;
    if (!db || ruleId == 0) return {};

    std::vector<Term> source;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT t.id, t.rule_id, COALESCE(r.code,''), t.term, t.term_norm, COALESCE(t.scope,'local'), "
            "COALESCE(t.origin,'manual'), COALESCE(t.list_name,''), "
            "COALESCE(t.source_doc,''), COALESCE(t.as_of_date,''), COALESCE(t.active,1) "
            "FROM terms t LEFT JOIN rules r ON r.id = t.rule_id "
            "WHERE t.rule_id=? AND COALESCE(t.list_name,'')=? ORDER BY t.term",
            -1, &st, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_int64(st, 1, ruleId);
    sqlite3_bind_text(st, 2, listName.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Term term;
        term.id = sqlite3_column_int64(st, 0);
        term.ruleId = sqlite3_column_int64(st, 1);
        term.ruleCode = ColumnText(st, 2);
        term.term = ColumnText(st, 3);
        term.termNorm = ColumnText(st, 4);
        term.scope = ColumnText(st, 5);
        term.origin = ColumnText(st, 6);
        term.listName = ColumnText(st, 7);
        term.sourceDoc = ColumnText(st, 8);
        term.asOfDate = ColumnText(st, 9);
        term.active = sqlite3_column_int(st, 10) != 0;
        source.push_back(std::move(term));
    }
    sqlite3_finalize(st);
    if (source.empty()) return {};

    std::string base = listName.empty() ? "Список" : listName;
    if (base.find("копия") == std::string::npos) base += " (копия)";
    std::string newName = UniqueListName(db, ruleId, base);

    if (!Begin()) return {};
    EnsureTermList(ruleId, newName);
    for (Term term : source) {
        term.id = 0;
        term.listName = newName;
        if (AddTerm(term)) ++copied;
    }
    if (!Commit()) {
        Rollback();
        return {};
    }
    return newName;
}

int Database::DeleteTerms(const std::vector<int64_t>& ids) {
    if (!db || ids.empty()) return 0;
    if (!Begin()) return 0;
    int removed = 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM terms WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (int64_t id : ids) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_DONE) removed += sqlite3_changes(db);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    if (!Commit()) {
        Rollback();
        return 0;
    }
    return removed;
}

int Database::DeleteTermLists(const std::vector<TermList>& lists) {
    if (!db || lists.empty()) return 0;
    if (!Begin()) return 0;
    int removed = 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "DELETE FROM terms WHERE rule_id=? AND COALESCE(list_name,'')=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (const TermList& list : lists) {
        sqlite3_bind_int64(st, 1, list.ruleId);
        sqlite3_bind_text(st, 2, list.name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_DONE) removed += sqlite3_changes(db);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    if (sqlite3_prepare_v2(db, "DELETE FROM term_lists WHERE rule_id=? AND name=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (const TermList& list : lists) {
        sqlite3_bind_int64(st, 1, list.ruleId);
        sqlite3_bind_text(st, 2, list.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    if (!Commit()) {
        Rollback();
        return 0;
    }
    return removed;
}

int Database::DeleteRulesWithoutTerms(const std::vector<int64_t>& ruleIds) {
    if (!db || ruleIds.empty()) return 0;
    if (!Begin()) return 0;
    int removed = 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "DELETE FROM rules WHERE id=? AND NOT EXISTS "
            "(SELECT 1 FROM terms WHERE rule_id = rules.id)", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (int64_t id : ruleIds) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_DONE) removed += sqlite3_changes(db);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    if (!Commit()) {
        Rollback();
        return 0;
    }
    return removed;
}

int Database::DeleteRules(const std::vector<int64_t>& ruleIds, int& removedTerms, int& removedFlags) {
    removedTerms = 0;
    removedFlags = 0;
    if (!db || ruleIds.empty()) return 0;
    if (!Begin()) return 0;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM flags WHERE rule_id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (int64_t id : ruleIds) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_DONE) removedFlags += sqlite3_changes(db);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db, "DELETE FROM terms WHERE rule_id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (int64_t id : ruleIds) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_DONE) removedTerms += sqlite3_changes(db);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    int removed = 0;
    if (sqlite3_prepare_v2(db, "DELETE FROM rules WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        Rollback();
        return 0;
    }
    for (int64_t id : ruleIds) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_DONE) removed += sqlite3_changes(db);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    if (!Commit()) {
        Rollback();
        return 0;
    }
    return removed;
}

int Database::CountRuleFlags(int64_t ruleId) {
    if (!db || ruleId == 0) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM flags WHERE rule_id=?", -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(st, 1, ruleId);
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

int Database::ApplyRules(int64_t runId) {
    if (!db) return 0;
    if (!Begin()) return 0;
    int flagged = ApplyRulesInternal(runId);
    if (flagged < 0) {
        Rollback();
        return 0;
    }
    if (!Commit()) {
        Rollback();
        return 0;
    }
    return flagged;
}

int Database::ApplyRulesInternal(int64_t runId) {
    if (!db) return -1;
    std::vector<Rule> rules = ListRules();
    std::vector<Term> terms = ListTerms();
    std::vector<ItemRecord> records = ListRunItems(runId);

    std::vector<Item> items;
    items.reserve(records.size());
    for (const ItemRecord& rec : records) items.push_back(rec.item);

    std::vector<std::vector<FlagResult>> flags = EvaluateRules(items, rules, terms);

    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM flags WHERE item_id IN (SELECT id FROM items WHERE run_id=?)",
                           -1, &del, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(del, 1, runId);
        sqlite3_step(del);
        sqlite3_finalize(del);
    }

    std::unordered_map<int64_t, int64_t> ruleCategory;
    for (const Rule& rule : rules) ruleCategory[rule.id] = rule.categoryId;

    sqlite3_stmt* insertFlag = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO flags(item_id, rule_id, severity, amount, rationale) VALUES(?,?,?,?,?)",
        -1, &insertFlag, nullptr);
    sqlite3_stmt* updateItem = nullptr;
    sqlite3_prepare_v2(db, "UPDATE items SET rule_id=?, category_id=?, severity=? WHERE id=?",
                       -1, &updateItem, nullptr);

    int flagged = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const std::vector<FlagResult>& itemFlags = flags[i];
        for (const FlagResult& flag : itemFlags) {
            sqlite3_bind_int64(insertFlag, 1, records[i].id);
            sqlite3_bind_int64(insertFlag, 2, flag.ruleId);
            sqlite3_bind_int(insertFlag, 3, flag.severity);
            sqlite3_bind_double(insertFlag, 4, flag.amount);
            sqlite3_bind_text(insertFlag, 5, flag.rationale.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(insertFlag);
            sqlite3_reset(insertFlag);
        }

        const FlagResult* top = nullptr;
        for (const FlagResult& flag : itemFlags) {
            if (!top || flag.severity > top->severity) top = &flag;
        }
        int64_t topRuleId = top ? top->ruleId : 0;
        int64_t topCategoryId = 0;
        auto catIt = ruleCategory.find(topRuleId);
        if (catIt != ruleCategory.end()) topCategoryId = catIt->second;
        sqlite3_bind_int64(updateItem, 1, topRuleId);
        sqlite3_bind_int64(updateItem, 2, topCategoryId);
        sqlite3_bind_int(updateItem, 3, top ? top->severity : 0);
        sqlite3_bind_int64(updateItem, 4, records[i].id);
        sqlite3_step(updateItem);
        sqlite3_reset(updateItem);
        if (top) ++flagged;
    }
    sqlite3_finalize(insertFlag);
    sqlite3_finalize(updateItem);

    return flagged;
}

std::vector<SummaryRow> Database::RunSummary(int64_t runId) {
    std::vector<SummaryRow> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT f.rule_id, COALESCE(r.code,''), COALESCE(c.name,''), COUNT(*), COALESCE(SUM(f.amount),0) "
            "FROM flags f JOIN items i ON i.id = f.item_id "
            "JOIN rules r ON r.id = f.rule_id "
            "LEFT JOIN categories c ON c.id = r.category_id "
            "WHERE i.run_id = ? GROUP BY f.rule_id ORDER BY r.code", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, runId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        SummaryRow row;
        row.ruleId = sqlite3_column_int64(st, 0);
        row.ruleCode = ColumnText(st, 1);
        row.category = ColumnText(st, 2);
        row.count = sqlite3_column_int(st, 3);
        row.amount = sqlite3_column_double(st, 4);
        out.push_back(std::move(row));
    }
    sqlite3_finalize(st);
    return out;
}

CompareResult Database::CompareRuns(int64_t runA, int64_t runB) {
    CompareResult res;

    struct Agg {
        std::string nameRaw;
        std::string subaccount;
        double close = 0.0;
        bool flagged = false;
        std::string rule;
        double flaggedAmount = 0.0;
    };

    auto aggregate = [&](int64_t runId, std::unordered_map<std::string, Agg>& map) {
        std::vector<ItemRecord> items = ListRunItems(runId);
        std::unordered_map<int64_t, double> flaggedAmountByItem;
        std::unordered_map<int64_t, bool> flaggedByItem;
        for (const Flag& flag : ListRunFlags(runId)) {
            flaggedAmountByItem[flag.itemId] += flag.amount;
            flaggedByItem[flag.itemId] = true;
        }
        for (const ItemRecord& rec : items) {
            std::string key = rec.item.nameNorm.empty() ? rec.item.nameRaw : rec.item.nameNorm;
            if (key.empty()) continue;
            Agg& agg = map[key];
            if (agg.nameRaw.empty()) {
                agg.nameRaw = rec.item.nameRaw;
                agg.subaccount = rec.subaccount;
            }
            agg.close += rec.item.closeDebit - rec.item.closeCredit;
            if (flaggedByItem.count(rec.id)) {
                agg.flagged = true;
                if (agg.rule.empty()) agg.rule = rec.ruleCode;
            }
            auto it = flaggedAmountByItem.find(rec.id);
            if (it != flaggedAmountByItem.end()) agg.flaggedAmount += it->second;
        }
    };

    std::unordered_map<std::string, Agg> mapA, mapB;
    aggregate(runA, mapA);
    aggregate(runB, mapB);

    std::unordered_map<std::string, bool> seen;
    for (const auto& entry : mapA) seen[entry.first] = true;
    for (const auto& entry : mapB) seen[entry.first] = true;

    for (const auto& kv : seen) {
        CompareRow row;
        row.nameNorm = kv.first;
        auto itA = mapA.find(kv.first);
        auto itB = mapB.find(kv.first);
        if (itA != mapA.end()) {
            row.inA = true;
            row.nameRaw = itA->second.nameRaw;
            row.subaccountA = itA->second.subaccount;
            row.closeA = itA->second.close;
            row.flaggedA = itA->second.flagged;
            row.ruleA = itA->second.rule;
            row.flaggedAmountA = itA->second.flaggedAmount;
            res.sumA += row.closeA;
            res.flaggedA += row.flaggedAmountA;
            if (row.flaggedA) res.flaggedCountA++;
        }
        if (itB != mapB.end()) {
            row.inB = true;
            if (row.nameRaw.empty()) {
                row.nameRaw = itB->second.nameRaw;
                row.subaccountB = itB->second.subaccount;
            }
            row.closeB = itB->second.close;
            row.flaggedB = itB->second.flagged;
            row.ruleB = itB->second.rule;
            row.flaggedAmountB = itB->second.flaggedAmount;
            res.sumB += row.closeB;
            res.flaggedB += row.flaggedAmountB;
            if (row.flaggedB) res.flaggedCountB++;
        }
        if (row.inA && row.inB) {
            res.both++;
            if (std::fabs(row.closeA - row.closeB) > 0.01) res.changed++;
        } else if (row.inA) {
            res.onlyA++;
        } else {
            res.onlyB++;
        }
        res.rows.push_back(std::move(row));
    }

    std::sort(res.rows.begin(), res.rows.end(), [](const CompareRow& a, const CompareRow& b) {
        double da = std::fabs(a.closeA - a.closeB);
        double db = std::fabs(b.closeA - b.closeB);
        if (da != db) return da > db;
        return a.nameNorm < b.nameNorm;
    });

    return res;
}

std::vector<SubaccountCompare> Database::CompareSubaccounts(int64_t runA, int64_t runB) {
    auto aggregate = [&](int64_t runId, std::unordered_map<std::string, SubaccountCompare>& map,
                         bool isA) {
        std::vector<ItemRecord> items = ListRunItems(runId);
        std::unordered_map<int64_t, std::string> subByItem;
        for (const ItemRecord& rec : items) {
            SubaccountCompare& agg = map[rec.subaccount];
            if (agg.code.empty()) agg.code = rec.subaccount;
            double balance = rec.item.closeDebit - rec.item.closeCredit;
            if (isA) agg.balanceA += balance;
            else agg.balanceB += balance;
            if (isA) agg.itemsA++;
            else agg.itemsB++;
            subByItem[rec.id] = rec.subaccount;
        }
        for (const Flag& flag : ListRunFlags(runId)) {
            auto it = subByItem.find(flag.itemId);
            if (it == subByItem.end()) continue;
            SubaccountCompare& agg = map[it->second];
            if (agg.code.empty()) agg.code = it->second;
            if (isA) {
                agg.flaggedA += flag.amount;
                agg.flaggedCountA++;
            } else {
                agg.flaggedB += flag.amount;
                agg.flaggedCountB++;
            }
        }
    };

    std::unordered_map<std::string, SubaccountCompare> map;
    aggregate(runA, map, true);
    aggregate(runB, map, false);

    std::vector<SubaccountCompare> out;
    out.reserve(map.size());
    for (const auto& entry : map) out.push_back(entry.second);
    std::sort(out.begin(), out.end(), [](const SubaccountCompare& a, const SubaccountCompare& b) {
        double da = std::fabs(a.flaggedB - a.flaggedA);
        double db = std::fabs(b.flaggedB - b.flaggedA);
        if (da != db) return da > db;
        return a.code < b.code;
    });
    return out;
}

std::vector<RuleCompare> Database::CompareRules(int64_t runA, int64_t runB) {
    std::unordered_map<int64_t, RuleCompare> map;
    for (const Flag& flag : ListRunFlags(runA)) {
        RuleCompare& agg = map[flag.ruleId];
        if (agg.ruleId == 0) {
            agg.ruleId = flag.ruleId;
            agg.ruleCode = flag.ruleCode;
            agg.category = flag.category;
        }
        agg.amountA += flag.amount;
        agg.countA++;
    }
    for (const Flag& flag : ListRunFlags(runB)) {
        RuleCompare& agg = map[flag.ruleId];
        if (agg.ruleId == 0) {
            agg.ruleId = flag.ruleId;
            agg.ruleCode = flag.ruleCode;
            agg.category = flag.category;
        }
        agg.amountB += flag.amount;
        agg.countB++;
    }

    std::vector<RuleCompare> out;
    out.reserve(map.size());
    for (const auto& entry : map) out.push_back(entry.second);
    std::sort(out.begin(), out.end(), [](const RuleCompare& a, const RuleCompare& b) {
        double da = std::fabs(a.amountB - a.amountA);
        double db = std::fabs(b.amountB - b.amountA);
        if (da != db) return da > db;
        return a.ruleCode < b.ruleCode;
    });
    return out;
}

std::string Database::ExportDictionaryPackage(const std::string& path) {
    std::ofstream out(path);
    if (!out) return {};
    out << "{\n  \"version\": 1,\n  \"categories\": [";
    std::vector<Category> cats = ListCategories();
    for (size_t i = 0; i < cats.size(); ++i) {
        const Category& c = cats[i];
        out << "\n    {\"name\":\"" << JsonEscape(c.name) << "\",\"color\":\"" << JsonEscape(c.color)
            << "\",\"description\":\"" << JsonEscape(c.description)
            << "\",\"sortOrder\":" << c.sortOrder << ",\"active\":" << (c.active ? "true" : "false") << "}";
        if (i + 1 < cats.size()) out << ",";
    }
    out << "],\n  \"rules\": [";
    std::vector<Rule> rules = ListRules();
    for (size_t i = 0; i < rules.size(); ++i) {
        const Rule& r = rules[i];
        out << "\n    {\"code\":\"" << JsonEscape(r.code) << "\",\"category\":\"" << JsonEscape(r.category)
            << "\",\"type\":\"" << JsonEscape(r.type) << "\",\"pattern\":\"" << JsonEscape(r.pattern)
            << "\",\"severity\":" << r.severity << ",\"rationale\":\"" << JsonEscape(r.rationale)
            << "\",\"sourceDoc\":\"" << JsonEscape(r.sourceDoc)
            << "\",\"active\":" << (r.active ? "true" : "false") << "}";
        if (i + 1 < rules.size()) out << ",";
    }
    out << "],\n  \"lists\": [";
    std::vector<TermList> lists = ListTermLists();
    for (size_t i = 0; i < lists.size(); ++i) {
        const TermList& l = lists[i];
        out << "\n    {\"ruleCode\":\"" << JsonEscape(l.ruleCode) << "\",\"name\":\"" << JsonEscape(l.name)
            << "\",\"description\":\"" << JsonEscape(l.description)
            << "\",\"sourceDoc\":\"" << JsonEscape(l.sourceDoc)
            << "\",\"asOfDate\":\"" << JsonEscape(l.asOfDate)
            << "\",\"sortOrder\":" << l.sortOrder << ",\"active\":" << (l.active ? "true" : "false") << "}";
        if (i + 1 < lists.size()) out << ",";
    }
    out << "],\n  \"terms\": [";
    std::vector<Term> terms = ListTerms();
    for (size_t i = 0; i < terms.size(); ++i) {
        const Term& t = terms[i];
        out << "\n    {\"ruleCode\":\"" << JsonEscape(t.ruleCode) << "\",\"term\":\"" << JsonEscape(t.term)
            << "\",\"scope\":\"" << JsonEscape(t.scope) << "\",\"origin\":\"" << JsonEscape(t.origin)
            << "\",\"listName\":\"" << JsonEscape(t.listName)
            << "\",\"sourceDoc\":\"" << JsonEscape(t.sourceDoc)
            << "\",\"asOfDate\":\"" << JsonEscape(t.asOfDate)
            << "\",\"active\":" << (t.active ? "true" : "false") << "}";
        if (i + 1 < terms.size()) out << ",";
    }
    out << "]\n}\n";
    return path;
}

bool Database::ImportDictionaryPackage(const std::string& path, DictImportStats& stats) {
    std::ifstream in(path);
    if (!in) return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    JsonValue root;
    size_t pos = 0;
    if (!ParseJsonValue(content, pos, root) || root.type != JsonValue::Object) return false;

    if (!Begin()) return false;

    const JsonValue* cats = root.get("categories");
    if (cats && cats->type == JsonValue::Array) {
        for (const JsonValue& v : cats->arr) {
            std::string name = v.getString("name");
            if (name.empty()) continue;
            Category c;
            c.name = name;
            c.color = v.getString("color", "#888888");
            c.description = v.getString("description");
            c.sortOrder = v.getInt("sortOrder", 0);
            c.active = v.getBool("active", true);
            int64_t existingId = 0;
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name = ?", -1, &st, nullptr);
            sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) existingId = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
            if (existingId) {
                c.id = existingId;
                if (UpdateCategory(c)) stats.categoriesUpdated++;
            } else if (AddCategory(c)) {
                stats.categoriesAdded++;
            }
        }
    }

    const JsonValue* rules = root.get("rules");
    if (rules && rules->type == JsonValue::Array) {
        for (const JsonValue& v : rules->arr) {
            std::string code = v.getString("code");
            if (code.empty()) continue;
            Rule r;
            r.code = code;
            r.type = v.getString("type");
            r.pattern = v.getString("pattern");
            r.severity = v.getInt("severity", 1);
            r.rationale = v.getString("rationale");
            r.sourceDoc = v.getString("sourceDoc");
            r.active = v.getBool("active", true);
            if (r.type.empty()) continue;

            std::string categoryName = v.getString("category");
            int64_t categoryId = 0;
            if (!categoryName.empty()) {
                sqlite3_stmt* catSt = nullptr;
                if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name = ?", -1, &catSt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(catSt, 1, categoryName.c_str(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(catSt) == SQLITE_ROW) categoryId = sqlite3_column_int64(catSt, 0);
                    sqlite3_finalize(catSt);
                }
            }
            if (categoryId == 0) categoryId = FallbackCategoryId(db);
            if (categoryId == 0) continue;
            r.categoryId = categoryId;

            int64_t existingId = 0;
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db, "SELECT id FROM rules WHERE code = ?", -1, &st, nullptr);
            sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) existingId = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
            if (existingId) {
                r.id = existingId;
                if (UpdateRule(r)) stats.rulesUpdated++;
            } else if (AddRule(r)) {
                stats.rulesAdded++;
            }
        }
    }

    const JsonValue* lists = root.get("lists");
    if (lists && lists->type == JsonValue::Array) {
        for (const JsonValue& v : lists->arr) {
            std::string name = v.getString("name");
            if (name.empty()) continue;
            std::string ruleCode = v.getString("ruleCode");
            int64_t ruleId = 0;
            if (!ruleCode.empty()) {
                sqlite3_stmt* ruleSt = nullptr;
                if (sqlite3_prepare_v2(db, "SELECT id FROM rules WHERE code = ?", -1, &ruleSt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(ruleSt, 1, ruleCode.c_str(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(ruleSt) == SQLITE_ROW) ruleId = sqlite3_column_int64(ruleSt, 0);
                    sqlite3_finalize(ruleSt);
                }
            }
            TermList l;
            l.ruleId = ruleId;
            l.name = name;
            l.description = v.getString("description");
            l.sourceDoc = v.getString("sourceDoc");
            l.asOfDate = v.getString("asOfDate");
            l.sortOrder = v.getInt("sortOrder", 0);
            l.active = v.getBool("active", true);
            int64_t existingId = 0;
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT id FROM term_lists WHERE rule_id=? AND name=?", -1, &st, nullptr) == SQLITE_OK) {
                if (ruleId == 0) {
                    sqlite3_bind_null(st, 1);
                } else {
                    sqlite3_bind_int64(st, 1, ruleId);
                }
                sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st) == SQLITE_ROW) existingId = sqlite3_column_int64(st, 0);
                sqlite3_finalize(st);
            }
            if (existingId) {
                l.id = existingId;
                if (UpdateTermList(l)) stats.listsUpdated++;
            } else if (AddTermList(l)) {
                stats.listsAdded++;
            }
        }
    }

    const JsonValue* terms = root.get("terms");
    if (terms && terms->type == JsonValue::Array) {
        for (const JsonValue& v : terms->arr) {
            std::string term = v.getString("term");
            if (term.empty()) continue;
            std::string scope = v.getString("scope", "local");
            std::string norm = NormalizeTextUtf8(term);
            int64_t existingId = 0;
            std::string existingOrigin;
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db,
                "SELECT id, origin FROM terms WHERE term_norm = ? AND scope = ? AND active = 1",
                -1, &st, nullptr);
            sqlite3_bind_text(st, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, scope.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                existingId = sqlite3_column_int64(st, 0);
                existingOrigin = ColumnText(st, 1);
            }
            sqlite3_finalize(st);
            if (existingId) {
                if (existingOrigin == "manual") {
                    sqlite3_prepare_v2(db,
                        "INSERT INTO conflicts(term_norm, manual_id, import_id, resolution, created_at) "
                        "VALUES(?,?,NULL,?,?)", -1, &st, nullptr);
                    sqlite3_bind_text(st, 1, norm.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(st, 2, existingId);
                    sqlite3_bind_text(st, 3, term.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st, 4, NowIso().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(st);
                    sqlite3_finalize(st);
                    stats.conflicts++;
                }
            } else {
                Term t;
                std::string ruleCode = v.getString("ruleCode");
                t.ruleId = 0;
                if (!ruleCode.empty()) {
                    sqlite3_stmt* ruleSt = nullptr;
                    if (sqlite3_prepare_v2(db, "SELECT id FROM rules WHERE code = ?", -1, &ruleSt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(ruleSt, 1, ruleCode.c_str(), -1, SQLITE_TRANSIENT);
                        if (sqlite3_step(ruleSt) == SQLITE_ROW) t.ruleId = sqlite3_column_int64(ruleSt, 0);
                        sqlite3_finalize(ruleSt);
                    }
                }
                t.term = term;
                t.scope = scope;
                t.origin = v.getString("origin", "import");
                t.listName = v.getString("listName");
                t.sourceDoc = v.getString("sourceDoc");
                t.asOfDate = v.getString("asOfDate");
                t.active = v.getBool("active", true);
                if (AddTerm(t)) stats.termsAdded++;
            }
        }
    }

    if (!Commit()) {
        Rollback();
        return false;
    }
    return true;
}
