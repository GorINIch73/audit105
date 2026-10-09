#include "../src/Database.h"
#include "../src/domain/FormatDetector.h"
#include "../src/domain/OdsReader.h"
#include "../src/domain/PositionParser.h"
#include "../src/domain/Utf8.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

static int failures = 0;

static void Check(bool condition, const std::string& what) {
    if (condition) {
        std::cout << "ok: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

static void TestUtf8() {
    Check(Utf8CodepointCount(u8"Ёлка") == 4, "utf8 codepoint count");
    Check(ToLowerUtf8(u8"ЯБЛОКО ПЯТЬ Ё") == u8"яблоко пять ё", "cyrillic lowercase");
    Check(ToUpperUtf8(u8"яблоко пять ё") == u8"ЯБЛОКО ПЯТЬ Ё", "cyrillic uppercase");
    Check(ToLowerUtf8(u8"ĐÜŸ") == u8"đüÿ", "latin-1 lowercase");
    Check(TrimUtf8(u8"\xc2\xa0  текст \xc2\xa0") == u8"текст", "trim utf8 nbsp");
    Check(CollapseSpacesUtf8(u8"  а\xc2\xa0\xc2\xa0б   в") == u8"а б в", "collapse utf8 spaces");
    Check(EqualsUtf8Insensitive(u8"Сумма", u8"СУММА"), "case-insensitive cyrillic equality");
    Check(ContainsUtf8(u8"Шприц одноразовый", u8"шприц"), "utf8 substring search");
    Check(!ContainsUtf8(u8"Шприц", u8"ШПРАЦ"), "utf8 substring negative");
}

static void TestNumbers() {
    bool ok = false;
    Check(std::fabs(ParseNumber(u8"37\u00A0744\u00A0129,43", ok) - 37744129.43) < 0.001 && ok,
          "number with utf8 nbsp and comma");
    Check(std::fabs(ParseNumber(u8"930,75", ok) - 930.75) < 0.001 && ok, "number with decimal comma");
    Check(std::fabs(ParseNumber(u8"-1 234,50", ok) + 1234.50) < 0.001 && ok, "negative number");
    Check(std::fabs(ParseNumber(u8"\u22125,10", ok) + 5.10) < 0.001 && ok, "unicode minus sign");
    Check(!ParseNumber(u8"Сумма", ok), "non-numeric cyrillic text rejected");
    Check(!ParseNumber(u8"52,60 р", ok), "number with cyrillic suffix rejected");
    Check(IsSubaccountCode(u8"105.31"), "subaccount code 105.31");
    Check(!IsSubaccountCode(u8"СМОФКабивен"), "name is not a subaccount code");
    Check(NormalizeText(u8"  Ёлка   Штырь ") == u8"ёлка штырь", "normalize cyrillic and whitespace");
    Check(NormalizeText(u8"ПЯТЬ\u00A0ШТ") == u8"пять шт", "normalize cyrillic with nbsp");
}

struct Expectations {
    int positions;
    int subaccounts;
    int exactDuplicates;
    int nameDuplicates;
    int noTurnover;
    int sampleIndex;
    int sampleRow;
    double sampleOpen;
    double sampleOut;
    double sampleClose;
    double samplePrice;
};

static void TestFile(const std::string& path, const Expectations& exp) {
    Spreadsheet sheet;
    std::string error;
    if (!ReadOds(path, sheet, error)) {
        std::cout << "FAIL: read " << path << ": " << error << "\n";
        ++failures;
        return;
    }

    Check(DetectFormat(path) == InputFormat::Ods, "format detected as ODS");
    Check(!sheet.sheetName.empty(), "sheet name found");

    ParseReport report = ParsePositions(sheet);

    std::cout << path << ": строк " << sheet.RowCount()
              << ", позиций " << report.items.size()
              << ", строк «Сумма» " << report.sumRows
              << ", строк «Кол.» " << report.qtyRows
              << ", подитогов " << report.subtotalRows
              << ", арифметика не сходится " << report.arithFail
              << ", без «Кол.» " << report.missingQty
              << ", точных дублей " << report.duplicates
              << ", дублей по имени " << report.nameDuplicates
              << ", без оборота " << report.noTurnover
              << ", остаток " << report.closingTotal << "\n";

    Check(static_cast<int>(report.items.size()) == exp.positions, "position count");
    Check(static_cast<int>(report.subaccounts.size()) == exp.subaccounts, "subaccount count");
    Check(report.arithFail == 0, "all positions arithmetically consistent");
    Check(report.duplicates == exp.exactDuplicates, "exact duplicate count");
    Check(report.nameDuplicates == exp.nameDuplicates, "name duplicate count");
    Check(report.noTurnover == exp.noTurnover, "no-turnover count");
    Check(report.qtyRows == report.sumRows + report.subtotalRows, "Сумма/Кол. rows paired");

    Check(report.subtotalArithFail == 0, "subtotals arithmetically consistent");

    const Item& sample = report.items[exp.sampleIndex];
    std::cout << "  sample row " << sample.rowNo << ": " << sample.nameRaw
              << " | " << sample.subaccount
              << " | open " << sample.openDebit << "/" << sample.openCredit
              << " | in " << sample.inDebit << " out " << sample.outCredit
              << " | close " << sample.closeDebit << "/" << sample.closeCredit
              << " | price " << sample.unitPrice << "\n";
    Check(sample.rowNo == exp.sampleRow, "sample row number");
    Check(std::fabs(sample.openDebit - exp.sampleOpen) < 0.01, "sample opening balance");
    Check(std::fabs(sample.outCredit - exp.sampleOut) < 0.01, "sample out credit");
    Check(std::fabs(sample.closeDebit - exp.sampleClose) < 0.01, "sample closing balance");
    Check(std::fabs(sample.unitPrice - exp.samplePrice) < 0.01, "sample unit price");

    Database db;
    const std::string dbPath = "/tmp/audit105_parser_test.db";
    std::remove(dbPath.c_str());
    std::remove((dbPath + "-wal").c_str());
    std::remove((dbPath + "-shm").c_str());
    if (!db.Open(dbPath)) {
        std::cout << "FAIL: cannot open test db\n";
        ++failures;
        return;
    }

    Run run;
    run.file = path;
    run.format = "ODS";
    run.organization = u8"Тестовая больница";
    run.period = "2026";
    run.items = static_cast<int>(report.items.size());

    int64_t runId = db.ImportRun(run, report);
    Check(runId != 0, "run imported");

    std::vector<ItemRecord> stored = db.ListRunItems(runId);
    Check(stored.size() == report.items.size(), "items round-trip count");

    double storedClosing = 0.0;
    for (const ItemRecord& rec : stored) {
        storedClosing += rec.item.closeDebit - rec.item.closeCredit;
    }
    Check(std::fabs(storedClosing - report.closingTotal) < 1.0, "items round-trip closing total");
    Check(db.ListSubaccounts(runId).size() == report.subaccounts.size(), "subaccounts round-trip count");

    std::vector<Flag> flags = db.ListRunFlags(runId);
    Check(!flags.empty(), "structural flags created on import");
    Run storedRun;
    Check(db.GetRun(runId, storedRun) && storedRun.flagged > 0, "run flagged count updated");

    Check(db.IntegrityReport().find(u8"Нарушений FK: 0") != std::string::npos, "no FK violations after import");
    std::cout << db.IntegrityReport() << "\n";
}

int main() {
    TestUtf8();
    TestNumbers();
    TestFile("tests/fixtures/105. 2026.ods",
             Expectations{1639, 7, 1, 38, 736, 1, 18, 18615.00, 930.75, 17684.25, 186.15});
    TestFile("tests/fixtures/105. 2025.ods",
             Expectations{1770, 7, 2, 44, 385, 0, 16, 6041.95, 20381.95, 18615.00, 186.15});

    if (failures == 0) {
        std::cout << "test_parser: OK\n";
        return 0;
    }
    std::cout << "test_parser: " << failures << " failures\n";
    return 1;
}
