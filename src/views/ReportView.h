#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class ReportView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

private:
    std::vector<Run> runs;
    int runIndex = 0;
    std::vector<ItemRecord> items;
    std::vector<Flag> flags;
    std::string lastExport;

    std::string ExportCsv();
    std::string ExportHtml();
};
