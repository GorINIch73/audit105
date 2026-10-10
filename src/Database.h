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
    int64_t ruleId = 0;
    std::string category;
    std::string ruleCode;
    int severity = 0;
};

struct Flag {
    int64_t id = 0;
    int64_t itemId = 0;
    int64_t ruleId = 0;
    std::string ruleCode;
    std::string category;
    int severity = 0;
    double amount = 0.0;
    std::string rationale;
};

struct Note {
    int64_t id = 0;
    int64_t itemId = 0;
    std::string note;
    std::string docNo;
    std::string docDate;
    std::string contract;
    std::string purchaseDate;
    std::string author;
    std::string createdAt;
};

struct SummaryRow {
    int64_t ruleId = 0;
    std::string ruleCode;
    std::string category;
    int count = 0;
    double amount = 0.0;
};

struct CompareRow {
    std::string nameNorm;
    std::string nameRaw;
    std::string subaccountA;
    std::string subaccountB;
    double closeA = 0.0;
    double closeB = 0.0;
    bool inA = false;
    bool inB = false;
    bool flaggedA = false;
    bool flaggedB = false;
    int64_t ruleIdA = 0;
    int64_t ruleIdB = 0;
    std::string ruleA;
    std::string ruleB;
    double flaggedAmountA = 0.0;
    double flaggedAmountB = 0.0;
};

struct CompareResult {
    std::vector<CompareRow> rows;
    int onlyA = 0;
    int onlyB = 0;
    int both = 0;
    int changed = 0;
    double sumA = 0.0;
    double sumB = 0.0;
    double flaggedA = 0.0;
    double flaggedB = 0.0;
    int flaggedCountA = 0;
    int flaggedCountB = 0;
};

struct SubaccountCompare {
    std::string code;
    double balanceA = 0.0;
    double balanceB = 0.0;
    double flaggedA = 0.0;
    double flaggedB = 0.0;
    int flaggedCountA = 0;
    int flaggedCountB = 0;
    int itemsA = 0;
    int itemsB = 0;
};

struct RuleCompare {
    int64_t ruleId = 0;
    std::string ruleCode;
    std::string category;
    double amountA = 0.0;
    double amountB = 0.0;
    int countA = 0;
    int countB = 0;
};

struct TermList {
    int64_t id = 0;
    int64_t ruleId = 0;
    std::string ruleCode;
    std::string name;
    std::string description;
    std::string sourceDoc;
    std::string asOfDate;
    int sortOrder = 0;
    bool active = true;
    int termCount = 0;
    int activeTermCount = 0;
};

struct DictImportStats {
    int categoriesAdded = 0;
    int categoriesUpdated = 0;
    int rulesAdded = 0;
    int rulesUpdated = 0;
    int listsAdded = 0;
    int listsUpdated = 0;
    int termsAdded = 0;
    int conflicts = 0;
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
    bool EnsureSeedExamples();
    bool SeedDefaults();
    bool SeedDefaultRules();
    bool SeedExampleRules();
    int CountRulesByCodePrefix(const std::string& prefix);

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
    std::vector<Flag> ListItemFlags(int64_t itemId);

    std::vector<Note> ListNotes(int64_t itemId);
    bool AddNote(const Note& note);
    bool UpdateNote(const Note& note);
    bool DeleteNote(int64_t id);
    int CountRunNotes(int64_t runId);
    bool InsertFlag(int64_t itemId, int64_t ruleId, int severity, double amount,
                    const std::string& rationale);

    std::vector<Category> ListCategories();
    bool AddCategory(const Category& cat);
    bool UpdateCategory(const Category& cat);
    bool DeleteCategory(int64_t id);
    int64_t CreateBlankCategory();
    int64_t DuplicateCategory(int64_t categoryId);
    bool GetCategory(int64_t id, Category& out);

    std::vector<Rule> ListRules();
    bool AddRule(const Rule& rule);
    bool UpdateRule(const Rule& rule);
    bool DeleteRule(int64_t id);
    int64_t CreateBlankRule(int64_t categoryId = 0);
    int64_t DuplicateRule(int64_t ruleId);
    bool GetRule(int64_t id, Rule& out);

    std::vector<Term> ListTerms();
    bool AddTerm(const Term& term);
    bool UpdateTerm(const Term& term);
    bool DeleteTerm(int64_t id);
    int64_t CreateBlankTerm(int64_t ruleId = 0, const std::string& listName = std::string());
    int64_t DuplicateTerm(int64_t termId);
    bool GetTerm(int64_t id, Term& out);
    std::vector<TermList> ListTermLists();
    bool TableExists(const std::string& table) const;
    bool GetTermList(int64_t id, TermList& out);
    bool AddTermList(const TermList& list);
    bool UpdateTermList(const TermList& list);
    bool DeleteTermList(int64_t id);
    int64_t CreateBlankList(int64_t ruleId = 0);
    int64_t DuplicateTermListById(int64_t id, int& copied);
    int64_t EnsureTermList(int64_t ruleId, const std::string& name);
    int CountRuleTerms(int64_t ruleId);
    int AddTermsToList(int64_t ruleId, const std::string& listName,
                       const std::vector<std::string>& terms,
                       const std::string& sourceDoc = std::string(),
                       const std::string& asOfDate = std::string());
    std::string DuplicateTermList(int64_t ruleId, const std::string& listName, int& copied);
    int DeleteTerms(const std::vector<int64_t>& ids);
    int DeleteTermLists(const std::vector<TermList>& lists);
    int DeleteRulesWithoutTerms(const std::vector<int64_t>& ruleIds);
    int DeleteRules(const std::vector<int64_t>& ruleIds, int& removedTerms, int& removedFlags);
    int CountRuleFlags(int64_t ruleId);

    int ApplyRules(int64_t runId);
    int ApplyRulesInternal(int64_t runId);
    std::vector<SummaryRow> RunSummary(int64_t runId);
    CompareResult CompareRuns(int64_t runA, int64_t runB);
    std::vector<SubaccountCompare> CompareSubaccounts(int64_t runA, int64_t runB);
    std::vector<RuleCompare> CompareRules(int64_t runA, int64_t runB);

    std::string ExportDictionaryPackage(const std::string& path);
    bool ImportDictionaryPackage(const std::string& path, DictImportStats& stats);

    int64_t LastInsertId() const;

private:
    sqlite3* db = nullptr;
    std::string path;
    int txnDepth = 0;
};
