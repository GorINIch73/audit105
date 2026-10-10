#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class RulesView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;

    void OpenTab(int tab);

private:
    int tab = 0;
    int runIndex = 0;

    std::vector<Run> runs;

    void RenderSummary();
    void RenderQueue();
};
