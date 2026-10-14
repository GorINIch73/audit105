#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class FormView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

    void SetRuleFilter(const std::string& code);
    void SelectRules(const std::vector<std::string>& codes);
    int ShownRows() const { return shown; }

private:
    std::vector<Run> runs;
    int runIndex = -1;
    std::vector<ItemRecord> items;
    std::vector<Flag> flags;
    std::vector<std::string> subcodes;
    std::vector<std::string> ruleCodes;
    std::vector<char> ruleSelected;
    std::unordered_map<int64_t, std::vector<std::string>> itemRules;
    std::vector<Category> categories;
    std::unordered_map<std::string, ImVec4> categoryColor;
    int64_t selectedId = 0;
    float listWidth = 700.0f;

    void RenderDetails(const ItemRecord& rec);
    void ReapplyRules();
    std::unordered_set<std::string> SelectedRules() const;

    std::string search;
    std::string subFilter;
    int turnoverMode = 0;
    double minSum = 0.0;
    bool onlyFlagged = false;

    int shown = 0;
    double shownSum = 0.0;
    std::vector<std::pair<std::string, int>> flagSummary;
};
