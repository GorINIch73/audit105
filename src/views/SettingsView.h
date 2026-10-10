#pragma once

#include "BaseView.h"
#include "../Database.h"
#include <string>

class SettingsView : public BaseView {
public:
    SettingsView() { SetTitle("Настройки"); }

    void Render() override;
    void RefreshData() override;

private:
    std::string integrity;
    std::string lastBackup;
    std::string lastPackage;
    DictImportStats lastStats;
    bool hasStats = false;
};
