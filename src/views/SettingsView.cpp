#include "SettingsView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../ImGuiFileDialog.h"
#include "../PlatformUtils.h"
#include "../Widgets.h"

#include <chrono>
#include <ctime>
#include <filesystem>

namespace {

std::string NowStamp() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

}  // namespace

void SettingsView::RefreshData() {
    if (app && app->HasDatabase()) integrity = app->Db().IntegrityReport();
}

void SettingsView::Render() {
    if (!app) return;

    if (BeginWindow()) {
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
        ImGui::Text(ICON_FA_BOOK "  Пакет справочников");
        ImGui::TextDisabled("Экспорт/импорт правил, категорий и терминов (JSON) — перенос настроек между базами.");

        if (ImGui::Button(ICON_FA_DOWNLOAD "  Экспорт пакета")) {
            std::string path = AppDataDir() + "/refs_bundle_" + NowStamp() + ".json";
            std::string written = app->Db().ExportDictionaryPackage(path);
            if (written.empty()) {
                app->ShowError("Не удалось записать пакет справочников");
            } else {
                lastPackage = written;
                app->SetStatus("Пакет справочников сохранён: " + written);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD "  Импорт пакета")) {
            IGFD::FileDialogConfig config;
            config.path = AppDataDir();
            app->OpenFileDialog("RefsBundle", "Импорт пакета справочников", "Пакет{.json}", config);
        }
        app->ClaimDialog("RefsBundle");
        if (ImGuiFileDialog::Instance()->Display("RefsBundle", ImGuiWindowFlags_NoCollapse,
                                                 ImVec2(600, 400), ImVec2(900, 700))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                std::string chosen = ImGuiFileDialog::Instance()->GetFilePathName();
                DictImportStats stats;
                if (app->Db().ImportDictionaryPackage(chosen, stats)) {
                    lastStats = stats;
                    hasStats = true;
                    app->SetStatus("Импортирован пакет: правил " + std::to_string(stats.rulesAdded) +
                                   ", терминов " + std::to_string(stats.termsAdded) +
                                   ", конфликтов " + std::to_string(stats.conflicts));
                } else {
                    app->ShowError("Не удалось импортировать пакет: " + chosen);
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }

        if (hasStats) {
            ImGui::TextDisabled("Импорт: категорий +%d (обновлено %d), правил +%d (обновлено %d), "
                                "терминов +%d, конфликтов %d",
                                lastStats.categoriesAdded, lastStats.categoriesUpdated,
                                lastStats.rulesAdded, lastStats.rulesUpdated,
                                lastStats.termsAdded, lastStats.conflicts);
        }

        EndWindow();
    }
}
