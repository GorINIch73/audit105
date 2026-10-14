#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>

class RulesTransferView : public BaseView {
public:
    void Render() override;
    void RefreshData() override {}

private:
    bool hasStats = false;
    DictImportStats lastStats;
    std::string lastPackage;
};