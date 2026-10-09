#pragma once

#include "BaseView.h"
#include "../domain/PositionParser.h"

#include <string>

class ImportView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

private:
    std::string path;
    std::string formatName;
    std::string sheetName;
    ParseReport report;
    bool parsed = false;
    std::string lastError;

    std::string organization;
    std::string period;
    std::string note;
    std::string account = "105";
    std::string kfo = "7";
};
