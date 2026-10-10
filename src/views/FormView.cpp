#include "FormView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

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

ImVec4 ParseColor(const std::string& hex) {
    int r = 255, g = 255, b = 255;
    if (hex.size() >= 7 && hex[0] == '#') {
        r = std::stoi(hex.substr(1, 2), nullptr, 16);
        g = std::stoi(hex.substr(3, 2), nullptr, 16);
        b = std::stoi(hex.substr(5, 2), nullptr, 16);
    }
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

double ArithGap(const Item& item) {
    return std::fabs(item.openDebit - item.openCredit + item.inDebit - item.outCredit -
                     (item.closeDebit - item.closeCredit));
}

}  // namespace

void FormView::RefreshData() {
    runs.clear();
    items.clear();
    subcodes.clear();
    categories.clear();
    if (!app || !app->HasDatabase()) return;

    runs = app->Db().ListRuns("", "", "", false);
    categories = app->Db().ListCategories();
    categoryColor.clear();
    for (const Category& cat : categories) categoryColor[cat.name] = ParseColor(cat.color);

    if (runIndex < 0 || runIndex >= static_cast<int>(runs.size())) runIndex = 0;
    if (!runs.empty()) {
        items = app->Db().ListRunItems(runs[runIndex].id);
        for (const Subaccount& sub : app->Db().ListSubaccounts(runs[runIndex].id)) {
            subcodes.push_back(sub.code);
        }
    }
    int64_t keep = selectedId;
    selectedId = 0;
    for (const ItemRecord& rec : items) {
        if (rec.id == keep) {
            selectedId = keep;
            break;
        }
    }
}

void FormView::ReapplyRules() {
    if (!app || !app->HasDatabase() || runs.empty()) return;
    if (runIndex < 0 || runIndex >= static_cast<int>(runs.size())) runIndex = 0;
    app->ReapplyRules(runs[static_cast<size_t>(runIndex)].id);
}

void FormView::RenderDetails(const ItemRecord& rec) {
    ImGui::TextWrapped("%s", rec.item.nameRaw.c_str());
    ImGui::TextDisabled("строка %d · субсчёт %s · строка с «Кол.»: %s",
                        rec.item.rowNo, rec.subaccount.c_str(),
                        rec.item.hasQtyRow ? "есть" : "нет");

    ImGui::Separator();
    ImGui::Text("Откр. %s + приход %s − расход %s = закр. %s",
                FormatAmount(rec.item.openDebit - rec.item.openCredit).c_str(),
                FormatAmount(rec.item.inDebit).c_str(),
                FormatAmount(rec.item.outCredit).c_str(),
                FormatAmount(rec.item.closeDebit - rec.item.closeCredit).c_str());
    if (rec.item.arithOk) {
        ImGui::TextDisabled("Арифметика сходится (расхождение %.2f)", ArithGap(rec.item));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "Арифметика не сходится: расхождение %s",
                           FormatAmount(ArithGap(rec.item)).c_str());
    }
    ImGui::Text("Количество: откр. %.2f, приход %.2f, расход %.2f, закр. %.2f · цена %.2f",
                rec.item.qtyOpen, rec.item.qtyIn, rec.item.qtyOut, rec.item.qtyClose,
                rec.item.unitPrice);

    ImGui::Separator();
    ImGui::Text(ICON_FA_GAVEL "  Сработавшие правила");
    std::vector<Flag> flags = app->Db().ListItemFlags(rec.id);
    if (flags.empty()) {
        ImGui::TextDisabled("Ни одно правило не сработало");
    } else {
        if (ImGui::BeginTable("item_flags", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Основание", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const Flag& flag : flags) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(flag.id));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(flag.id));
                ImGui::TableNextColumn();
                ImGui::Text("%s", flag.ruleCode.c_str());
                ImGui::TableNextColumn();
                auto it = categoryColor.find(flag.category);
                ImVec4 color = (it != categoryColor.end()) ? it->second : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                ImGui::TextColored(color, "%s", flag.category.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(flag.amount).c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", flag.rationale.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::Text(ICON_FA_NOTE_STICKY "  Примечания аудитора");
    std::vector<Note> notes = app->Db().ListNotes(rec.id);
    if (notes.empty()) ImGui::TextDisabled("Примечаний нет");

    for (Note& note : notes) {
        ImGui::PushID(static_cast<int>(note.id));
        InputTextSync("Накладная №", note.docNo);
        ImGui::SameLine();
        InputTextSync("Дата накладной", note.docDate);
        InputTextSync("Договор", note.contract);
        ImGui::SameLine();
        InputTextSync("Дата закупки", note.purchaseDate);
        InputTextSync("Примечание", note.note);
        ImGui::SameLine();
        InputTextSync("Кто внёс", note.author);
        ImGui::TextDisabled("создано: %s", note.createdAt.c_str());
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Сохранить")) {
            if (app->Db().UpdateNote(note)) app->SetStatus("Примечание сохранено");
        }
        ImGui::SameLine();
        std::string modalId = "delete_note_" + std::to_string(note.id);
        if (ImGui::Button(ICON_FA_TRASH "  Удалить")) ImGui::OpenPopup(modalId.c_str());
        if (ConfirmationModal(modalId, "Удалить примечание",
                             "Удалить это примечание без возможности восстановления?",
                             "Удалить", "Отмена")) {
            if (app->Db().DeleteNote(note.id)) {
                app->SetStatus("Примечание удалено");
                notes = app->Db().ListNotes(rec.id);
            }
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    if (ImGui::Button(ICON_FA_PLUS "  Добавить примечание")) {
        Note note;
        note.itemId = rec.id;
        if (app->Db().AddNote(note)) {
            app->SetStatus("Примечание добавлено");
            Run run;
            if (app->Db().GetRun(runs[runIndex].id, run)) {
                app->Db().UpdateRunStats(run.id, run.items, run.flagged,
                                        app->Db().CountRunNotes(run.id));
            }
        }
    }
}

void FormView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта. Откройте или создайте её (Файл → Создать/Открыть).");
            EndWindow();
            return;
        }

        if (runs.empty()) RefreshData();

        if (runs.empty()) {
            ImGui::TextDisabled("Нет прогонов. Импортируйте оборот (Импорт → Импорт оборота).");
            EndWindow();
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

        const int64_t currentRunId = runs[static_cast<size_t>(runIndex)].id;
        if (ImGui::Button(ICON_FA_ROTATE "  Обновить отметки")) ReapplyRules();
        if (app->RulesOutOfDate(currentRunId)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                               "%s  Правила изменились — отметки пересчитаны", ICON_FA_ROTATE);
            ReapplyRules();
        }

        ImGui::Separator();

        InputTextSync(ICON_FA_MAGNIFYING_GLASS "  Поиск", search);
        ImGui::SameLine();
        InputTextSync("Субсчёт", subFilter);
        ImGui::SameLine();
        ImGui::Combo(ICON_FA_FILTER "  Фильтр", &turnoverMode, TurnoverModes, 6);
        ImGui::SameLine();
        AmountInput("Мин. остаток", minSum);
        ImGui::SameLine();
        ImGui::Checkbox("Только отмеченные", &onlyFlagged);

        flagSummary.clear();
        std::unordered_map<std::string, int> counts;
        for (const ItemRecord& rec : items) {
            if (!rec.ruleCode.empty()) ++counts[rec.ruleCode];
        }
        flagSummary.assign(counts.begin(), counts.end());
        std::sort(flagSummary.begin(), flagSummary.end(),
                  [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                      return a.first < b.first;
                  });

        ImGui::Separator();

        const float available = ImGui::GetContentRegionAvail().x;
        float height = ImGui::GetContentRegionAvail().y;
        if (height < 320.0f) height = 320.0f;
        const float minLeft = 300.0f;
        const float maxLeft = std::max(minLeft, available - 260.0f);
        listWidth = std::clamp(listWidth, minLeft, maxLeft);

        shown = 0;
        shownSum = 0.0;

        ImGui::BeginChild("mz_table", ImVec2(listWidth, height), true);
        const int columns = 11;
        if (ImGui::BeginTable("mz_table", columns,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
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
                if (onlyFlagged && rec.ruleCode.empty()) continue;
                if (minSum > 0.0 && item.closeDebit - item.closeCredit < minSum) continue;
                if (!subFilter.empty() && !ContainsUtf8(rec.subaccount, subFilter)) continue;
                if (!search.empty() && !ContainsUtf8(item.nameNorm, ToLowerUtf8(search))) continue;

                ++shown;
                shownSum += item.closeDebit - item.closeCredit;

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(rec.id));
                auto it = categoryColor.find(rec.category);
                if (!item.arithOk) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(255, 89, 89, 90));
                } else if (it != categoryColor.end()) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                          IM_COL32(static_cast<int>(it->second.x * 255),
                                                   static_cast<int>(it->second.y * 255),
                                                   static_cast<int>(it->second.z * 255), 90));
                }

                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(rec.id));
                ImGui::TableNextColumn();
                ImGui::Text("%d", item.rowNo);
                ImGui::TableNextColumn();
                bool selected = (selectedId == rec.id);
                if (ImGui::Selectable(item.nameRaw.c_str(), selected)) selectedId = rec.id;
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
                else if (!rec.ruleCode.empty()) ImGui::Text("%s", rec.ruleCode.c_str());
                else ImGui::TextDisabled("—");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        VerticalSplitter("mz_split", &listWidth, minLeft, maxLeft, 10.0f, height);
        ImGui::SameLine();

        ImGui::BeginChild("mz_details", ImVec2(available - listWidth - 14.0f, height), true);
        ItemRecord selected;
        bool found = false;
        for (const ItemRecord& rec : items) {
            if (rec.id == selectedId) {
                selected = rec;
                found = true;
                break;
            }
        }
        if (!found) {
            ImGui::TextDisabled("Выберите строку в таблице — здесь появятся правила,");
            ImGui::TextDisabled("арифметика и примечания аудитора.");
        } else {
            RenderDetails(selected);
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Показано: %d из %d | сумма остатков: %s", shown, static_cast<int>(items.size()),
                    FormatAmount(shownSum).c_str());
        if (!flagSummary.empty()) {
            std::string summary;
            for (const auto& entry : flagSummary) {
                if (!summary.empty()) summary += ", ";
                summary += entry.first + " (" + std::to_string(entry.second) + ")";
            }
            ImGui::Text("Отметки: %s", summary.c_str());
        }
        ImGui::TextDisabled(
            "Легенда: фон строки = цвет категории; красный — арифметика не сходится.");

        std::string runModalId = "delete_run_" + std::to_string(runs[runIndex].id);
        if (ImGui::Button(ICON_FA_TRASH "  Удалить прогон")) ImGui::OpenPopup(runModalId.c_str());
        if (ConfirmationModal(runModalId, "Удалить прогон",
                             "Удалить прогон #" + std::to_string(runs[runIndex].id) +
                             " и все его позиции? Отменить это нельзя.",
                             "Удалить", "Отмена")) {
            int removedItems = 0;
            double removedSum = 0.0;
            if (app->Db().DeleteRun(runs[runIndex].id, removedItems, removedSum)) {
                app->SetStatus("Удалён прогон: " + std::to_string(removedItems) + " позиций");
                runIndex = -1;
                RefreshData();
            }
        }

        EndWindow();
    }
}
