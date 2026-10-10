#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class CompareView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

private:
    std::vector<Run> runs;
    int runAIndex = 0;
    int runBIndex = 1;
    CompareResult result;
    std::vector<SubaccountCompare> subRows;
    std::vector<RuleCompare> ruleRows;
    int tab = 0;
    std::string filter;
    bool onlyChanged = false;
    std::string lastExport;

    std::string ExportCsv();
    std::string ExportHtml();
};
