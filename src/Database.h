#pragma once

#include "domain/PositionParser.h"

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
    bool DeleteRun(int64_t id, int& removedItems, double& removedSum);
    bool UpdateRunStats(int64_t id, int items, int flagged, int noted);

    int64_t InsertSubaccount(int64_t runId, const Subaccount& sub);
    int64_t InsertItem(int64_t runId, int64_t subaccountId, const Item& item);

    std::vector<ItemRecord> ListRunItems(int64_t runId);
    std::vector<Subaccount> ListSubaccounts(int64_t runId);

    std::vector<Category> ListCategories();
    bool AddCategory(const Category& cat);
    bool UpdateCategory(const Category& cat);
    bool DeleteCategory(int64_t id);

    int64_t LastInsertId() const;

private:
    sqlite3* db = nullptr;
    std::string path;
};
