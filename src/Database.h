#pragma once

#include "domain/PositionParser.h"
#include "domain/RulesEngine.h"

#include <sqlite3.h>
#include <cstdint>
#include <string>
#include <vector>

struct Run {
    int64_t id = 0;
    std::string file;
    std::string format;
    std::string organization;
    std::string period;
    std::string note;
    std::string account = "105";
    std::string kfo = "7";
    std::string importedAt;
    int items = 0;
    int flagged = 0;
    int noted = 0;
};

struct Category {
    int64_t id = 0;
    std::string name;
    std::string color;
    std::string description;
    int sortOrder = 0;
    bool active = true;
};

struct ItemRecord {
    int64_t id = 0;
    Item item;
    std::string subaccount;
    std::string category;
    std::string ruleCode;
    int severity = 0;
};

struct Flag {
    int64_t id = 0;
    int64_t itemId = 0;
    std::string ruleCode;
    std::string category;
    int severity = 0;
    double amount = 0.0;
    std::string rationale;
};

struct SummaryRow {
    std::string ruleCode;
    std::string category;
    int count = 0;
    double amount = 0.0;
};

class Database {
public:
    Database() = default;
    ~Database();

    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const { return db != nullptr; }
    const std::string& Path() const { return path; }

    bool EnsureSchema();
    bool SeedDefaults();
    bool SeedDefaultRules();

    bool Backup(const std::string& reason, std::string& outPath);
    bool SaveAs(const std::string& target);

    bool Begin();
    bool Commit();
    bool Rollback();

    std::string IntegrityReport();

    int64_t InsertRun(const Run& run);
    int64_t ImportRun(const Run& run, const ParseReport& report);
    std::vector<Run> ListRuns(const std::string& organization,
                              const std::string& period,
                              const std::string& note,
                              bool recentOnly,
                              int limit = 200);
    bool GetRun(int64_t id, Run& out);
    int CountRunsByFile(const std::string& file);
    bool DeleteRun(int64_t id, int& removedItems, double& removedSum);
    bool UpdateRunStats(int64_t id, int items, int flagged, int noted);

    int64_t InsertSubaccount(int64_t runId, const Subaccount& sub);
    int64_t InsertItem(int64_t runId, int64_t subaccountId, const Item& item);

    std::vector<ItemRecord> ListRunItems(int64_t runId);
    std::vector<Subaccount> ListSubaccounts(int64_t runId);
    std::vector<Flag> ListRunFlags(int64_t runId);
    bool InsertFlag(int64_t itemId, const std::string& ruleCode, const std::string& category,
                    int severity, double amount, const std::string& rationale);

    std::vector<Category> ListCategories();
    bool AddCategory(const Category& cat);
    bool UpdateCategory(const Category& cat);
    bool DeleteCategory(int64_t id);

    std::vector<Rule> ListRules();
    bool AddRule(const Rule& rule);
    bool UpdateRule(const Rule& rule);
    bool DeleteRule(int64_t id);

    std::vector<Term> ListTerms();
    bool AddTerm(const Term& term);
    bool DeleteTerm(int64_t id);

    int ApplyRules(int64_t runId);
    int ApplyRulesInternal(int64_t runId);
    std::vector<SummaryRow> RunSummary(int64_t runId);

    int64_t LastInsertId() const;

private:
    sqlite3* db = nullptr;
    std::string path;
};
