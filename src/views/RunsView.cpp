#include "RunsView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"
#include "FormView.h"
#include "ImportView.h"

#include <algorithm>
#include <string>

void RunsView::RefreshData() {
    if (!app || !app->HasDatabase()) {
        runs.clear();
        return;
    }
    runs = app->Db().ListRuns(orgFilter, periodFilter, noteFilter, recentOnly);
    if (selectedId != 0) {
        bool stillThere = false;
        for (const Run& run : runs) {
            if (run.id == selectedId) {
                stillThere = true;
                break;
            }
        }
        if (!stillThere) selectedId = 0;
    }
}

void RunsView::RenderTable() {
    if (runs.empty()) {
        ImGui::TextDisabled("Нет прогонов по текущим фильтрам. Импортируйте оборот (Импорт → Импорт оборота).");
        return;
    }

    if (ImGui::BeginTable("runs_table", 9,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Файл", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Формат", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Учреждение", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Период", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Импорт", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Позиций", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Отмечено", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Действия", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableHeadersRow();

        for (const Run& run : runs) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(run.id));
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(run.id));
            ImGui::TableNextColumn();
            bool selected = (selectedId == run.id);
            if (ImGui::Selectable(run.file.c_str(), selected)) selectedId = static_cast<int>(run.id);
            ImGui::TableNextColumn();
            ImGui::Text("%s", run.format.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", run.organization.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", run.period.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", run.importedAt.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", run.items);
            ImGui::TableNextColumn();
            ImGui::Text("%d", run.flagged);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_FA_STETHOSCOPE "  Форма")) {
                app->CreateView<FormView>("Форма МЗ");
                app->SetStatus("Открыта форма для прогона #" + std::to_string(run.id));
            }
            ImGui::SameLine();
            std::string modalId = "delete_run_" + std::to_string(run.id);
            if (ImGui::SmallButton(ICON_FA_TRASH "  удалить")) ImGui::OpenPopup(modalId.c_str());
            if (ConfirmationModal(modalId, "Удалить прогон",
                                 "Удалить прогон #" + std::to_string(run.id) +
                                 " («" + run.file + "») и все его позиции? Отменить это нельзя.",
                                 "Удалить", "Отмена")) {
                int removedItems = 0;
                double removedSum = 0.0;
                if (app->Db().DeleteRun(run.id, removedItems, removedSum)) {
                    app->SetStatus("Удалён прогон: " + std::to_string(removedItems) + " позиций");
                    RefreshData();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void RunsView::RenderActions() {
    ImGui::Separator();
    ImGui::Text("Всего прогонов: %zu", runs.size());
    int totalItems = 0;
    int totalFlagged = 0;
    for (const Run& run : runs) {
        totalItems += run.items;
        totalFlagged += run.flagged;
    }
    ImGui::Text("Позиций: %d · отмечено: %d", totalItems, totalFlagged);

    ImGui::Separator();
    if (ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "  Резервная копия базы")) {
        if (app->BackupDatabase("manual")) app->SetStatus("Резервная копия создана");
        else app->ShowError("Не удалось создать резервную копию");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UPLOAD "  Импорт нового оборота")) {
        app->CreateView<ImportView>("Импорт оборота");
    }
}

void RunsView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }
        if (runs.empty()) RefreshData();

        InputTextSync(ICON_FA_BUILDING "  Учреждение", orgFilter);
        ImGui::SameLine();
        InputTextSync("Период/год", periodFilter, 120.0f);
        ImGui::SameLine();
        InputTextSync("Примечание", noteFilter, 160.0f);
        ImGui::SameLine();
        ImGui::Checkbox(ICON_FA_CLOCK "  Последние 30 дней", &recentOnly);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILTER "  Применить")) RefreshData();

        ImGui::Separator();
        RenderTable();
        RenderActions();

        EndWindow();
    }
}
