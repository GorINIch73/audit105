#include "SettingsView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"

void SettingsView::RefreshData() {
    if (app && app->HasDatabase()) integrity = app->Db().IntegrityReport();
}

void SettingsView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        RenderCloseBar();

        ImGui::Text(ICON_FA_DATABASE "  База: %s",
                    app->DbPath().empty() ? "не выбрана" : app->DbPath().c_str());
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "  Резервная копия")) {
            if (app->BackupDatabase("manual")) {
                RefreshData();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SHIELD "  Отчёт о целостности")) {
            RefreshData();
        }

        ImGui::Separator();
        ImGui::TextWrapped("%s", integrity.empty() ? "База не открыта." : integrity.c_str());

        ImGui::Separator();
        ImGui::Text(ICON_FA_CLOCK_ROTATE_LEFT "  История баз");
        if (app->recentDbPaths.empty()) {
            ImGui::TextDisabled("История пуста.");
        } else {
            for (const auto& path : app->recentDbPaths) {
                if (ImGui::Button(path.c_str())) app->OpenDatabase(path);
            }
        }

        ImGui::Separator();
        ImGui::Text("Справочники: %s", app->refsPath.c_str());
        ImGui::TextDisabled("Пакет справочников (refs_bundle.zip) — этап 9");

        EndWindow();
    }
}
