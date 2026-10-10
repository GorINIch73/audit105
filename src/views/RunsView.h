#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class RunsView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

private:
    std::vector<Run> runs;
    std::string orgFilter;
    std::string periodFilter;
    std::string noteFilter;
    bool recentOnly = false;
    int selectedId = 0;

    void RenderTable();
    void RenderActions();
};
