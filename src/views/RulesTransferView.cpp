#include "RulesTransferView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../ImGuiFileDialog.h"
#include "../PlatformUtils.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

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

#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>

void RulesTransferView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("Сначала создайте или откройте базу (Файл → Создать/Открыть).");
            EndWindow();
            return;
        }

        ImGui::Text(ICON_FA_BOOK "  Импорт и экспорт правил/справочников");
        ImGui::TextDisabled("Пакет JSON содержит категории, правила и термины — перенос настроек между базами.");

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DOWNLOAD "  Экспорт пакета (выбрать файл)")) {
            IGFD::FileDialogConfig config;
            config.path = AppDataDir();
            config.filePathName = AppDataDir() + "/refs_bundle_" + NowStamp() + ".json";
            app->OpenFileDialog("RulesExport", "Сохранить пакет правил", "Пакет{.json}", config);
        }
        app->ClaimDialog("RulesExport");
        if (ImGuiFileDialog::Instance()->Display("RulesExport", ImGuiWindowFlags_NoCollapse,
                                                ImVec2(600, 400), ImVec2(900, 700))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                std::string chosen = ImGuiFileDialog::Instance()->GetFilePathName();
                std::string written = app->Db().ExportDictionaryPackage(chosen);
                if (written.empty()) {
                    app->ShowError("Не удалось записать пакет: " + chosen);
                } else {
                    lastPackage = written;
                    app->SetStatus("Пакет сохранён: " + written);
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }
        if (!lastPackage.empty()) ImGui::TextDisabled("%s", lastPackage.c_str());

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_UPLOAD "  Импорт пакета")) {
            IGFD::FileDialogConfig config;
            config.path = AppDataDir();
            app->OpenFileDialog("RulesPackage", "Импорт пакета правил", "Пакет{.json}", config);
        }
        app->ClaimDialog("RulesPackage");
        if (ImGuiFileDialog::Instance()->Display("RulesPackage", ImGuiWindowFlags_NoCollapse,
                                                 ImVec2(600, 400), ImVec2(900, 700))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                std::string chosen = ImGuiFileDialog::Instance()->GetFilePathName();
                DictImportStats stats;
                if (app->Db().ImportDictionaryPackage(chosen, stats)) {
                    lastStats = stats;
                    hasStats = true;
                    app->MarkRulesChanged();
                    app->RefreshAllViews();
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
            ImGui::Separator();
            ImGui::TextDisabled("Импорт: категорий +%d (обновлено %d), правил +%d (обновлено %d), "
                                "терминов +%d, конфликтов %d",
                                lastStats.categoriesAdded, lastStats.categoriesUpdated,
                                lastStats.rulesAdded, lastStats.rulesUpdated,
                                lastStats.termsAdded, lastStats.conflicts);
        }

        EndWindow();
    }
}