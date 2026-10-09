#include "FormView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <string>

namespace {

const char* TurnoverModes[] = {"Все", "Есть приход", "Есть расход", "Без оборота",
                               "Без количества", "Арифметика не сходится"};

bool MatchesTurnover(const Item& item, int mode) {
    switch (mode) {
        case 1: return item.inDebit > 0.0;
        case 2: return item.outCredit > 0.0;
        case 3: return item.inDebit <= 0.0 && item.outCredit <= 0.0;
        case 4: return item.qtyOpen == 0.0 && item.qtyIn == 0.0 && item.qtyOut == 0.0 && item.qtyClose == 0.0;
        case 5: return !item.arithOk;
        default: return true;
    }
}

ImVec4 RowColor(const ItemRecord& rec) {
    if (!rec.item.arithOk) return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    if (rec.item.inDebit <= 0.0 && rec.item.outCredit <= 0.0) return ImVec4(1.0f, 0.90f, 0.45f, 1.0f);
    if (!rec.ruleCode.empty()) return ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
    return ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
}

}  // namespace

void FormView::RefreshData() {
    runs.clear();
    items.clear();
    subcodes.clear();
    if (!app || !app->HasDatabase()) return;

    runs = app->Db().ListRuns("", "", "", false);
    if (runIndex < 0 || runIndex >= static_cast<int>(runs.size())) runIndex = 0;
    if (!runs.empty()) {
        items = app->Db().ListRunItems(runs[runIndex].id);
        for (const Subaccount& sub : app->Db().ListSubaccounts(runs[runIndex].id)) {
            subcodes.push_back(sub.code);
        }
    }
}

void FormView::Render() {
    if (!app) return;

    if (ImGui::Begin(GetTitle(), nullptr)) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта. Откройте или создайте её (Файл → Создать/Открыть).");
            ImGui::End();
            return;
        }

        if (runs.empty()) RefreshData();

        if (runs.empty()) {
            ImGui::TextDisabled("Нет прогонов. Импортируйте оборот (Импорт → Импорт оборота).");
            ImGui::End();
            return;
        }

        std::vector<std::string> runLabels;
        runLabels.reserve(runs.size());
        for (const Run& run : runs) {
            std::string label = "#" + std::to_string(run.id) + "  " + run.file;
            if (!run.period.empty()) label += " | " + run.period;
            if (!run.organization.empty()) label += " | " + run.organization;
            runLabels.push_back(label);
        }
        auto runGetter = [](void* data, int idx) -> const char* {
            return (*reinterpret_cast<std::vector<std::string>*>(data))[idx].c_str();
        };
        if (ImGui::Combo(ICON_FA_DATABASE "  Прогон", &runIndex, runGetter, &runLabels,
                         static_cast<int>(runLabels.size()))) {
            RefreshData();
        }

        ImGui::Separator();

        InputTextSync(ICON_FA_MAGNIFYING_GLASS "  Поиск", search);
        ImGui::SameLine();
        InputTextSync("Субсчёт", subFilter);
        ImGui::SameLine();
                ImGui::Combo(ICON_FA_FILTER "  Фильтр", &turnoverMode, TurnoverModes, 6);
        ImGui::SameLine();
        AmountInput("Мин. остаток", minSum);

        ImGui::Separator();

        shown = 0;
        shownSum = 0.0;

        const int columns = 11;
        if (ImGui::BeginTable("mz_table", columns,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Стр.", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Номенклатура", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Субсчёт", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Откр.", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Приход", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Расход", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Закр.", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Кол.", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Цена", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Отметка", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            for (const ItemRecord& rec : items) {
                const Item& item = rec.item;
                if (!MatchesTurnover(item, turnoverMode)) continue;
                if (minSum > 0.0 && item.closeDebit - item.closeCredit < minSum) continue;
                if (!subFilter.empty() && !ContainsUtf8(rec.subaccount, subFilter)) continue;
                if (!search.empty() && !ContainsUtf8(item.nameNorm, ToLowerUtf8(search))) continue;

                ++shown;
                shownSum += item.closeDebit - item.closeCredit;

                ImGui::TableNextRow();
                ImVec4 color = RowColor(rec);
                if (color.w > 0.0f) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                          IM_COL32(static_cast<int>(color.x * 255),
                                                   static_cast<int>(color.y * 255),
                                                   static_cast<int>(color.z * 255), 90));
                }

                ImGui::TableNextColumn();
                ImGui::Text("%d", shown);
                ImGui::TableNextColumn();
                ImGui::Text("%d", item.rowNo);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", item.nameRaw.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", rec.subaccount.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(item.openDebit - item.openCredit).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(item.inDebit).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(item.outCredit).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(item.closeDebit - item.closeCredit).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f/%.2f", item.qtyOpen, item.qtyClose);
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(item.unitPrice).c_str());
                ImGui::TableNextColumn();
                if (!item.arithOk) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "арифм.");
                else if (item.inDebit <= 0.0 && item.outCredit <= 0.0) ImGui::TextColored(ImVec4(1.0f, 0.90f, 0.45f, 1.0f), "без оборота");
                else if (!rec.ruleCode.empty()) ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s", rec.ruleCode.c_str());
                else ImGui::TextDisabled("—");
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Показано: %d из %d | сумма остатков: %s", shown, static_cast<int>(items.size()),
                    FormatAmount(shownSum).c_str());
        ImGui::TextDisabled(
            "Легенда: красный — арифметика не сходится; жёлтый — без оборота; синий — отмечено правилом.");

        ImGui::End();
    }
}
