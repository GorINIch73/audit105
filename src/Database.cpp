#include "Database.h"

#include "PlatformUtils.h"
#include "domain/Utf8.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
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
  rule_code TEXT, category TEXT, severity INTEGER DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_items_run ON items(run_id);
CREATE INDEX IF NOT EXISTS idx_items_sub ON items(subaccount_id);
CREATE INDEX IF NOT EXISTS idx_items_flag ON items(rule_code);

CREATE TABLE IF NOT EXISTS flags(
  id INTEGER PRIMARY KEY, item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
  rule_code TEXT NOT NULL, category TEXT NOT NULL, severity INTEGER DEFAULT 1,
  amount REAL, rationale TEXT);
CREATE INDEX IF NOT EXISTS idx_flags_item ON flags(item_id);

CREATE TABLE IF NOT EXISTS notes(
  id INTEGER PRIMARY KEY, item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
  note TEXT, doc_no TEXT, doc_date TEXT, contract TEXT, purchase_date TEXT,
  author TEXT, created_at TEXT);
CREATE INDEX IF NOT EXISTS idx_notes_item ON notes(item_id);

CREATE TABLE IF NOT EXISTS categories(
  id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, color TEXT NOT NULL,
  description TEXT, sort_order INTEGER DEFAULT 0, active INTEGER DEFAULT 1);

CREATE TABLE IF NOT EXISTS rules(
  id INTEGER PRIMARY KEY, code TEXT UNIQUE,
  category TEXT NOT NULL REFERENCES categories(name),
  type TEXT NOT NULL,
  pattern TEXT NOT NULL, severity INTEGER DEFAULT 1,
  rationale TEXT, source_doc TEXT, active INTEGER DEFAULT 1, updated_at TEXT);

CREATE TABLE IF NOT EXISTS terms(
  id INTEGER PRIMARY KEY, rule_code TEXT REFERENCES rules(code),
  term TEXT NOT NULL, term_norm TEXT NOT NULL,
  scope TEXT DEFAULT 'local',
  origin TEXT DEFAULT 'manual',
  list_name TEXT, source_doc TEXT, as_of_date TEXT,
  active INTEGER DEFAULT 1, created_at TEXT, updated_at TEXT);
CREATE UNIQUE INDEX IF NOT EXISTS idx_terms_norm ON terms(term_norm, scope) WHERE active=1;

CREATE TABLE IF NOT EXISTS kinds(id INTEGER PRIMARY KEY, kind TEXT UNIQUE,
  shelf_life_months INTEGER, consumable INTEGER DEFAULT 0, note TEXT);

CREATE TABLE IF NOT EXISTS sources(id INTEGER PRIMARY KEY, name TEXT NOT NULL, file TEXT,
  as_of_date TEXT, imported_at TEXT, rows INTEGER, active INTEGER DEFAULT 1);

CREATE TABLE IF NOT EXISTS conflicts(
  id INTEGER PRIMARY KEY, term_norm TEXT NOT NULL,
  manual_id INTEGER REFERENCES terms(id), import_id INTEGER REFERENCES terms(id),
  resolved INTEGER DEFAULT 0, resolution TEXT, created_at TEXT);
)sql";

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
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::EnsureSchema() {
    char* err = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Schema error: " << (err ? err : "unknown") << std::endl;
        sqlite3_free(err);
        return false;
    }

    bool hasHasQty = false;
    sqlite3_stmt* st = nullptr;
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

bool Database::SeedDefaults() {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM categories", -1, &st, nullptr) != SQLITE_OK)
        return false;
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (count > 0) return true;

    struct Seed { const char* name; const char* color; const char* desc; int order; };
    const Seed seeds[] = {
        {"Недопустимо по ОМС", "#C0392B", "Закупка не финансируется ОМС", 1},
        {"Неверный субсчёт", "#E67E22", "Позиция не на том субсчёте", 2},
        {"Обратить внимание", "#F1C40F", "Требует ручной проверки документов", 3},
    };
    sqlite3_prepare_v2(db,
        "INSERT INTO categories(name, color, description, sort_order) VALUES(?,?,?,?)",
        -1, &st, nullptr);
    for (const auto& s : seeds) {
        sqlite3_bind_text(st, 1, s.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, s.color, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, s.desc, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, s.order);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    if (SeedDefaultRules()) return true;
    return true;
}

bool Database::SeedDefaultRules() {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM rules", -1, &st, nullptr) != SQLITE_OK)
        return false;
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (count > 0) return true;

    struct SeedRule {
        const char* code;
        const char* category;
        const char* type;
        const char* pattern;
        int severity;
        const char* rationale;
    };
    const SeedRule seeds[] = {
        {"S01", "Обратить внимание", "structural", "arith", 3, "Сальдо не сходится: начало + оборот != конец"},
        {"S02", "Обратить внимание", "structural", "qty_missing", 2, "Нет строки «Кол.» — количество неизвестно"},
        {"S03", "Обратить внимание", "structural", "qty_mismatch", 1, "Слоты «Сумма» и «Кол.» заполнены неравномерно"},
        {"S04", "Обратить внимание", "structural", "negative", 3, "Отрицательный остаток на конец периода"},
        {"S05", "Обратить внимание", "structural", "no_turnover", 1, "Позиция без оборота за период"},
        {"S06", "Обратить внимание", "structural", "zero_qty", 2, "Сумма есть, количество нулевое"},
        {"S07", "Обратить внимание", "exact", "signature", 3, "Точный дубликат позиции (совпадают все суммы)"},
        {"S08", "Обратить внимание", "exact", "name", 2, "Дубликат наименования на том же субсчёте"},
    };
    sqlite3_prepare_v2(db,
        "INSERT INTO rules(code, category, type, pattern, severity, rationale, active, updated_at) "
        "VALUES(?,?,?,?,?,?,1,?)", -1, &st, nullptr);
    for (const auto& s : seeds) {
        sqlite3_bind_text(st, 1, s.code, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, s.category, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, s.type, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 4, s.pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 5, s.severity);
        sqlite3_bind_text(st, 6, s.rationale, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 7, NowCompact().c_str(), -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    return true;
}

bool Database::Begin() { return sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) == SQLITE_OK; }
bool Database::Commit() { return sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK; }
bool Database::Rollback() { return sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK; }

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
            "i.rule_code, i.category, i.severity, i.has_qty, i.qty_mismatch "
            "FROM items i JOIN subaccounts s ON s.id = i.subaccount_id "
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
        rec.ruleCode = ColumnText(st, 17);
        rec.category = ColumnText(st, 18);
        rec.severity = sqlite3_column_int(st, 19);
        rec.item.subaccount = ColumnText(st, 4);
        rec.item.hasQtyRow = sqlite3_column_int(st, 20) != 0;
        rec.item.qtyMismatch = sqlite3_column_int(st, 21) != 0;
        out.push_back(std::move(rec));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::InsertFlag(int64_t itemId, const std::string& ruleCode, const std::string& category,
                          int severity, double amount, const std::string& rationale) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO flags(item_id, rule_code, category, severity, amount, rationale) VALUES(?,?,?,?,?,?)",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, itemId);
    sqlite3_bind_text(st, 2, ruleCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, severity);
    sqlite3_bind_double(st, 5, amount);
    sqlite3_bind_text(st, 6, rationale.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_ROW || sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Flag> Database::ListRunFlags(int64_t runId) {
    std::vector<Flag> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT f.id, f.item_id, f.rule_code, f.category, f.severity, f.amount, f.rationale "
            "FROM flags f JOIN items i ON i.id = f.item_id WHERE i.run_id = ? "
            "ORDER BY f.item_id, f.severity DESC", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, runId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Flag flag;
        flag.id = sqlite3_column_int64(st, 0);
        flag.itemId = sqlite3_column_int64(st, 1);
        flag.ruleCode = ColumnText(st, 2);
        flag.category = ColumnText(st, 3);
        flag.severity = sqlite3_column_int(st, 4);
        flag.amount = sqlite3_column_double(st, 5);
        flag.rationale = ColumnText(st, 6);
        out.push_back(std::move(flag));
    }
    sqlite3_finalize(st);
    return out;
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

bool Database::AddCategory(const Category& cat) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO categories(name, color, description, sort_order, active) VALUES(?,?,?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, cat.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, cat.color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, cat.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, cat.sortOrder);
    sqlite3_bind_int(st, 5, cat.active ? 1 : 0);
    bool ok = sqlite3_step(st) == SQLITE_ROW || sqlite3_changes(db) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateCategory(const Category& cat) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE categories SET name=?, color=?, description=?, sort_order=?, active=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, cat.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, cat.color.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, cat.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, cat.sortOrder);
    sqlite3_bind_int(st, 5, cat.active ? 1 : 0);
    sqlite3_bind_int64(st, 6, cat.id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db) > 0;
}

bool Database::DeleteCategory(int64_t id) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return sqlite3_changes(db) > 0;
}

std::vector<Rule> Database::ListRules() {
    std::vector<Rule> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id, code, category, type, pattern, severity, rationale, source_doc, active "
            "FROM rules ORDER BY code", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Rule rule;
        rule.id = sqlite3_column_int64(st, 0);
        rule.code = ColumnText(st, 1);
        rule.category = ColumnText(st, 2);
        rule.type = ColumnText(st, 3);
        rule.pattern = ColumnText(st, 4);
        rule.severity = sqlite3_column_int(st, 5);
        rule.rationale = ColumnText(st, 6);
        rule.sourceDoc = ColumnText(st, 7);
        rule.active = sqlite3_column_int(st, 8) != 0;
        out.push_back(std::move(rule));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::AddRule(const Rule& rule) {
    if (rule.code.empty() || rule.category.empty() || rule.type.empty()) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO rules(code, category, type, pattern, severity, rationale, source_doc, active, updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, rule.code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, rule.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, rule.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, rule.pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, rule.severity);
    sqlite3_bind_text(st, 6, rule.rationale.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, rule.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, rule.active ? 1 : 0);
    sqlite3_bind_text(st, 9, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return ok;
}

bool Database::UpdateRule(const Rule& rule) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "UPDATE rules SET category=?, type=?, pattern=?, severity=?, rationale=?, source_doc=?, active=?, updated_at=? "
            "WHERE id=?", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, rule.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, rule.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, rule.pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, rule.severity);
    sqlite3_bind_text(st, 5, rule.rationale.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, rule.sourceDoc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, rule.active ? 1 : 0);
    sqlite3_bind_text(st, 8, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 9, rule.id);
    bool ok = sqlite3_changes(db) > 0;
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
            "SELECT id, rule_code, term, term_norm, scope, origin, list_name, active "
            "FROM terms ORDER BY term_norm", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Term term;
        term.id = sqlite3_column_int64(st, 0);
        term.ruleCode = ColumnText(st, 1);
        term.term = ColumnText(st, 2);
        term.termNorm = ColumnText(st, 3);
        term.scope = ColumnText(st, 4);
        term.origin = ColumnText(st, 5);
        term.listName = ColumnText(st, 6);
        term.active = sqlite3_column_int(st, 7) != 0;
        out.push_back(std::move(term));
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::AddTerm(const Term& term) {
    if (term.term.empty()) return false;
    Term copy = term;
    copy.termNorm = NormalizeTextUtf8(copy.term);
    if (copy.scope.empty()) copy.scope = "local";
    if (copy.origin.empty()) copy.origin = "manual";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO terms(rule_code, term, term_norm, scope, origin, list_name, active, created_at, updated_at) "
            "VALUES(?,?,?,?,?,?,?, ?, ?)", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, copy.ruleCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, copy.term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, copy.termNorm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, copy.scope.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, copy.origin.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, copy.listName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, copy.active ? 1 : 0);
    sqlite3_bind_text(st, 8, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, NowCompact().c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
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

    std::cerr << "DBG apply rules=" << rules.size() << " items=" << items.size() << " totalflags=";
    size_t total = 0;
    for (auto& f : flags) total += f.size();
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM flags WHERE item_id IN (SELECT id FROM items WHERE run_id=?)",
                           -1, &del, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(del, 1, runId);
        sqlite3_step(del);
        sqlite3_finalize(del);
    }

    sqlite3_stmt* insertFlag = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO flags(item_id, rule_code, category, severity, amount, rationale) VALUES(?,?,?,?,?,?)",
        -1, &insertFlag, nullptr);
    sqlite3_stmt* updateItem = nullptr;
    sqlite3_prepare_v2(db, "UPDATE items SET rule_code=?, category=?, severity=? WHERE id=?",
                       -1, &updateItem, nullptr);

    int flagged = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const std::vector<FlagResult>& itemFlags = flags[i];
        for (const FlagResult& flag : itemFlags) {
            sqlite3_bind_int64(insertFlag, 1, records[i].id);
            sqlite3_bind_text(insertFlag, 2, flag.ruleCode.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insertFlag, 3, flag.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insertFlag, 4, flag.severity);
            sqlite3_bind_double(insertFlag, 5, flag.amount);
            sqlite3_bind_text(insertFlag, 6, flag.rationale.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(insertFlag);
            sqlite3_reset(insertFlag);
        }

        const FlagResult* top = nullptr;
        for (const FlagResult& flag : itemFlags) {
            if (!top || flag.severity > top->severity) top = &flag;
        }
        sqlite3_bind_text(updateItem, 1, top ? top->ruleCode.c_str() : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updateItem, 2, top ? top->category.c_str() : "", -1, SQLITE_TRANSIENT);
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
            "SELECT f.rule_code, f.category, COUNT(*), COALESCE(SUM(f.amount),0) "
            "FROM flags f JOIN items i ON i.id = f.item_id WHERE i.run_id = ? "
            "GROUP BY f.rule_code, f.category ORDER BY f.rule_code", -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(st, 1, runId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        SummaryRow row;
        row.ruleCode = ColumnText(st, 0);
        row.category = ColumnText(st, 1);
        row.count = sqlite3_column_int(st, 2);
        row.amount = sqlite3_column_double(st, 3);
        out.push_back(std::move(row));
    }
    sqlite3_finalize(st);
    return out;
}
