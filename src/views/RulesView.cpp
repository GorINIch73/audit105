#include "RulesView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"

#include <algorithm>
#include <unordered_map>

namespace {

std::string RunLabel(const Run& run) {
    std::string label = "#" + std::to_string(run.id) + "  " + run.file;
    if (!run.period.empty()) label += " | " + run.period;
    return label;
}

}  // namespace

void RulesView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    runs = app->Db().ListRuns("", "", "", false);
    if (runIndex < 0 || runIndex >= static_cast<int>(runs.size())) runIndex = 0;
}

void RulesView::OpenTab(int tabIndex) {
    tab = tabIndex == 0 ? 0 : 1;
    RefreshData();
}

void RulesView::RenderSummary() {
    if (runs.empty()) {
        ImGui::TextDisabled("Нет прогонов — импортируйте оборот.");
        return;
    }

    std::vector<std::string> labels;
    for (const Run& run : runs) labels.push_back(RunLabel(run));
    auto getter = [](void* data, int idx) -> const char* {
        return (*reinterpret_cast<std::vector<std::string>*>(data))[idx].c_str();
    };
    ImGui::Combo(ICON_FA_DATABASE "  Прогон", &runIndex, getter, &labels, static_cast<int>(labels.size()));

    const Run& run = runs[static_cast<size_t>(runIndex)];
    if (app->RulesOutOfDate(run.id)) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "%s  Правила изменились — отметки пересчитаны", ICON_FA_ROTATE);
        app->ReapplyRules(run.id);
        RefreshData();
    }
    ImGui::Text("Позиций: %d · отмечено: %d · примечаний: %d", run.items, run.flagged, run.noted);
    ImGui::Separator();

    std::vector<SummaryRow> summary = app->Db().RunSummary(run.id);
    if (summary.empty()) {
        ImGui::TextDisabled("Ни одно правило не сработало");
        return;
    }

    if (ImGui::BeginTable("summary_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Позиций", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();
        double totalAmount = 0.0;
        int totalCount = 0;
        int rowId = 1;
        for (const SummaryRow& row : summary) {
            ImGui::TableNextRow();
            ImGui::PushID(rowId);
            ImGui::TableNextColumn();
            ImGui::Text("%d", rowId);
            ImGui::TableNextColumn();
            ImGui::Text("%s", row.ruleCode.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", row.category.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", row.count);
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatAmount(row.amount).c_str());
            ImGui::PopID();
            totalAmount += row.amount;
            totalCount += row.count;
            ++rowId;
        }
        ImGui::EndTable();
        ImGui::Text("Всего отметок: %d, сумма: %s", totalCount, FormatAmount(totalAmount).c_str());
    }

    if (ImGui::Button(ICON_FA_GAVEL "  Переприменить правила к прогону")) {
        app->ReapplyRules(run.id);
        RefreshData();
    }
}

void RulesView::RenderQueue() {
    if (runs.empty()) {
        ImGui::TextDisabled("Нет прогонов.");
        return;
    }

    std::vector<std::string> labels;
    for (const Run& run : runs) labels.push_back(RunLabel(run));
    auto getter = [](void* data, int idx) -> const char* {
        return (*reinterpret_cast<std::vector<std::string>*>(data))[idx].c_str();
    };
    ImGui::Combo(ICON_FA_DATABASE "  Прогон", &runIndex, getter, &labels, static_cast<int>(labels.size()));

    std::vector<ItemRecord> items = app->Db().ListRunItems(runs[static_cast<size_t>(runIndex)].id);
    std::vector<ItemRecord> unflagged;
    unflagged.reserve(items.size());
    for (const ItemRecord& rec : items) {
        if (rec.ruleCode.empty()) unflagged.push_back(rec);
    }
    std::sort(unflagged.begin(), unflagged.end(), [](const ItemRecord& a, const ItemRecord& b) {
        return a.item.closeDebit - a.item.closeCredit > b.item.closeDebit - b.item.closeCredit;
    });

    ImGui::Text("Не распознано ни одним правилом: %zu позиций", unflagged.size());
    ImGui::Separator();

    std::unordered_map<std::string, int> tokens;
    for (const ItemRecord& rec : unflagged) {
        std::string name = rec.item.nameNorm;
        size_t pos = 0;
        while (pos < name.size()) {
            size_t next = name.find_first_of(" ,;.-()/№", pos);
            std::string token = name.substr(pos, next - pos);
            if (token.size() >= 4) ++tokens[token];
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }
    std::vector<std::pair<std::string, int>> candidates(tokens.begin(), tokens.end());
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    ImGui::Text(ICON_FA_LIGHTBULB "  Частотные кандидаты в термины (топ-20)");
    for (size_t i = 0; i < std::min<size_t>(20, candidates.size()); ++i) {
        ImGui::Text("%s — %d", candidates[i].first.c_str(), candidates[i].second);
    }

    ImGui::Separator();
    if (ImGui::BeginTable("queue_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Стр.", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Номенклатура", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Субсчёт", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Остаток", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < std::min<size_t>(50, unflagged.size()); ++i) {
            const ItemRecord& rec = unflagged[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(rec.id));
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(rec.id));
            ImGui::TableNextColumn();
            ImGui::Text("%d", rec.item.rowNo);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", rec.item.nameRaw.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", rec.subaccount.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatAmount(rec.item.closeDebit - rec.item.closeCredit).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void RulesView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }

        if (runs.empty()) RefreshData();

        const char* tabs[] = {"Сводка", "Очередь"};
        if (ImGui::BeginTabBar("rules_tabs")) {
            for (int i = 0; i < 2; ++i) {
                if (ImGui::BeginTabItem(tabs[i])) {
                    tab = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        if (tab == 0) {
            RenderSummary();
        } else {
            RenderQueue();
        }

        EndWindow();
    }
}
