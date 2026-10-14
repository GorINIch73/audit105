#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <unordered_map>
#include <vector>

class ReportView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

    void SelectRules(const std::vector<std::string>& codes);
    size_t FilteredRowCount() const { return FilteredRows().size(); }

    std::string ExportCsv();
    std::string ExportHtml();

private:
    std::vector<Run> runs;
    int runIndex = 0;
    std::vector<ItemRecord> items;
    std::vector<Flag> flags;
    std::vector<Category> categories;
    std::unordered_map<std::string, ImVec4> categoryColor;
    std::unordered_map<std::string, std::string> categoryHex;

    std::vector<std::string> ruleCodes;
    std::vector<char> ruleSelected;
    std::string categoryFilter;
    std::string subFilter;
    std::string search;
    double minSum = 0.0;
    bool onlyFlagged = true;
    int rowLimit = 200;

    std::string lastExport;

    std::vector<ItemRecord> FilteredRows() const;
    std::vector<Flag> FilteredFlags() const;
    std::string FilterLabel() const;
};
