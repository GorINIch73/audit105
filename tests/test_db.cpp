#include "../src/Database.h"

#include <iostream>
#include <string>

static int failures = 0;

static void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    } else {
        std::cout << "ok: " << what << "\n";
    }
}

int main() {
    const std::string path = "/tmp/audit105_test.db";
    Database db;
    if (!db.Open(path)) {
        std::cerr << "open failed\n";
        return 1;
    }

    Check(db.ListCategories().size() == 3, "seeded categories");

    Run r;
    r.file = "105. 2026.ods";
    r.format = "ods";
    r.organization = "ГБ №1";
    r.period = "2026";
    r.note = "проверка";
    int64_t id = db.InsertRun(r);
    Check(id > 0, "insert run");

    Run r2;
    r2.file = "105. 2025.ods";
    r2.format = "ods";
    r2.organization = "ГБ №2";
    r2.period = "2025";
    int64_t id2 = db.InsertRun(r2);

    Check(db.UpdateRunStats(id, 1639, 120, 5), "update run stats");

    Run got;
    Check(db.GetRun(id, got) && got.items == 1639, "get run with stats");

    Check(db.ListRuns("", "", "", false).size() == 2, "list runs");
    Check(db.ListRuns("ГБ №1", "", "", false).size() == 1, "filter by organization");
    Check(db.ListRuns("", "2025", "", false).size() == 1, "filter by period");

    std::string backup;
    Check(db.Backup("test", backup), "backup");

    std::cout << db.IntegrityReport() << "\n";

    int removed = 0;
    double sum = 0;
    Check(db.DeleteRun(id2, removed, sum), "delete run");
    Check(db.ListRuns("", "", "", false).size() == 1, "run removed");

    std::cout << (failures ? "test_db: FAILED\n" : "test_db: OK\n");
    return failures ? 1 : 0;
}
