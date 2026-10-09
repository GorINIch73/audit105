#pragma once

#include "imgui.h"
#include "IconsFontAwesome6.h"
#include <string>

struct AboutInfo {
    std::string name = "Аудит счёта 105";
    std::string version = "0.1 (этап 0)";
    std::string dbPath;
    std::string refsPath;
};

inline void RenderAboutDialog(const AboutInfo& info, bool* show) {
    if (!*show) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("О программе", nullptr, flags);
    ImGui::TextUnformatted(ICON_FA_STETHOSCOPE " Аудит счёта 105 (МЗ, ОМС)");
    ImGui::Separator();
    ImGui::Text("Версия: %s", info.version.c_str());
    ImGui::Text("База: %s", info.dbPath.empty() ? "не выбрана" : info.dbPath.c_str());
    ImGui::Text("Справочники: %s", info.refsPath.empty() ? "refs/" : info.refsPath.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Чтение выгрузок 1С (ODS, XLSX, XML, XLS через soffice), "
                       "разбор позиций оборотов счёта 105, правила категоризации, "
                       "примечания аудитора, отчёты HTML/CSV.");
    if (ImGui::Button("Закрыть")) {
        *show = false;
    }
    ImGui::End();
}
