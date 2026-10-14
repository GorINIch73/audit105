#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class ManualTermsImportView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

private:
    struct CsvTermRow {
        std::string term;
        std::string profile;
        std::string ruleCode;
        std::string listName;
        std::string sourceDoc;
        std::string asOfDate;
        std::string scope;
        bool active = true;
    };

    std::vector<Rule> rules;

    std::string csvPath;
    bool csvParsed = false;
    bool csvParseAttempted = false;
    std::string csvError;
    std::vector<CsvTermRow> csvRows;
    int csvBadRows = 0;
    int csvImported = 0;
    int csvUpdated = 0;
    int csvConflicts = 0;
    int csvSkipped = 0;

    void ParseCsvTerms();
    void ImportCsvTerms();
    std::string ProfileToRuleCode(const std::string& profile) const;
};