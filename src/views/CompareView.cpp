#include "CompareView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../PlatformUtils.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string NowStamp() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

std::string CsvEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (char ch : text) {
        if (ch == '"') out += "\"\"";
        else out.push_back(ch);
    }
    return "\"" + out + "\"";
}

std::string HtmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(ch);
        }
    }
    return out;
}

std::string ReportDirectory() {
    std::string dir = AppDataDir() + "/reports";
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

void CompareView::RefreshData() {
    if (!app || !app->HasDatabase()) {
        runs.clear();
        return;
    }
    runs = app->Db().ListRuns("", "", "", false);
    if (runs.empty()) return;
    if (runAIndex < 0 || runAIndex >= static_cast<int>(runs.size())) runAIndex = 0;
    if (runBIndex < 0 || runBIndex >= static_cast<int>(runs.size()))
        runBIndex = (runs.size() > 1) ? 1 : 0;
    if (runAIndex == runBIndex && runs.size() > 1) runBIndex = (runAIndex == 0) ? 1 : 0;
    result = app->Db().CompareRuns(runs[static_cast<size_t>(runAIndex)].id,
                                   runs[static_cast<size_t>(runBIndex)].id);
    subRows = app->Db().CompareSubaccounts(runs[static_cast<size_t>(runAIndex)].id,
                                           runs[static_cast<size_t>(runBIndex)].id);
    ruleRows = app->Db().CompareRules(runs[static_cast<size_t>(runAIndex)].id,
                                      runs[static_cast<size_t>(runBIndex)].id);
}

std::string CompareView::ExportCsv() {
    if (runs.empty()) return {};
    std::string path = ReportDirectory() + "/compare_" +
                       std::to_string(runs[static_cast<size_t>(runAIndex)].id) + "_" +
                       std::to_string(runs[static_cast<size_t>(runBIndex)].id) + "_" +
                       NowStamp() + ".csv";
    std::ofstream out(path);
    if (!out) return {};

    out << "name,subaccount_a,subaccount_b,close_a,close_b,delta,in_a,in_b,flagged_a,flagged_b,"
        "rule_a,rule_b,flagged_amount_a,flagged_amount_b\n";
    for (const CompareRow& row : result.rows) {
        out << CsvEscape(row.nameRaw) << "," << CsvEscape(row.subaccountA) << ","
            << CsvEscape(row.subaccountB) << "," << row.closeA << "," << row.closeB << ","
            << (row.closeB - row.closeA) << "," << (row.inA ? 1 : 0) << "," << (row.inB ? 1 : 0)
            << "," << (row.flaggedA ? 1 : 0) << "," << (row.flaggedB ? 1 : 0) << ","
            << CsvEscape(row.ruleA) << "," << CsvEscape(row.ruleB) << ","
            << row.flaggedAmountA << "," << row.flaggedAmountB << "\n";
    }
    return path;
}

std::string CompareView::ExportHtml() {
    if (runs.empty()) return {};
    const Run& a = runs[static_cast<size_t>(runAIndex)];
    const Run& b = runs[static_cast<size_t>(runBIndex)];
    std::string path = ReportDirectory() + "/compare_" + std::to_string(a.id) + "_" +
                       std::to_string(b.id) + "_" + NowStamp() + ".html";
    std::ofstream out(path);
    if (!out) return {};

    out << "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>Сравнение прогонов</title>"
        "<style>body{font-family:sans-serif;margin:24px}table{border-collapse:collapse;margin:12px 0}"
        "th,td{border:1px solid #999;padding:6px 10px}th{background:#e8eef3}"
        ".pos{color:#0a6b3f}.neg{color:#8f1b1b}</style></head><body>";
    out << "<h1>Аудит счёта 105 — сравнение прогонов</h1>";
    out << "<p>Прогон A: #" << a.id << " (" << HtmlEscape(a.file) << ", " << HtmlEscape(a.period)
        << ") · Прогон B: #" << b.id << " (" << HtmlEscape(b.file) << ", " << HtmlEscape(b.period)
        << ")</p>";
    out << "<p>Сопоставление только по нормализованному названию, без объединения данных.<br>"
        "В обоих: " << result.both << " (из них изменились: " << result.changed << ") · "
        "только в A: " << result.onlyA << " · только в B: " << result.onlyB << "</p>";
    out << "<p>Отмечено в A: " << result.flaggedCountA << " (" << FormatAmount(result.flaggedA)
        << " ₽) · в B: " << result.flaggedCountB << " (" << FormatAmount(result.flaggedB)
        << " ₽) · динамика отмеченных: " << FormatAmount(result.flaggedB - result.flaggedA) << " ₽</p>";

    out << "<h2>Динамика позиций</h2><table><tr><th>Номенклатура</th><th>Остаток A</th>"
        "<th>Остаток B</th><th>Δ</th><th>Отмечено A</th><th>Отмечено B</th></tr>";
    for (const CompareRow& row : result.rows) {
        double delta = row.closeB - row.closeA;
        std::string cls = delta > 0.01 ? "pos" : (delta < -0.01 ? "neg" : "");
        out << "<tr><td>" << HtmlEscape(row.nameRaw) << "</td><td>" << FormatAmount(row.closeA)
            << "</td><td>" << FormatAmount(row.closeB) << "</td><td class=\"" << cls << "\">"
            << FormatAmount(delta) << "</td><td>" << (row.flaggedA ? HtmlEscape(row.ruleA) : "")
            << "</td><td>" << (row.flaggedB ? HtmlEscape(row.ruleB) : "") << "</td></tr>";
    }
    out << "</table></body></html>";
    return path;
}

void CompareView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }
        if (runs.empty()) RefreshData();
        if (runs.size() < 2) {
            ImGui::TextDisabled("Нужно минимум два прогона для сравнения. Импортируйте оборот.");
            EndWindow();
            return;
        }

        auto getter = [](void* data, int idx) -> const char* {
            return (*reinterpret_cast<std::vector<std::string>*>(data))[idx].c_str();
        };
        std::vector<std::string> labels;
        for (const Run& run : runs) {
            std::string label = "#" + std::to_string(run.id) + "  " + run.file;
            if (!run.period.empty()) label += " | " + run.period;
            labels.push_back(label);
        }
        ImGui::Text(ICON_FA_CODE_COMPARE "  Прогон A (базовый)");
        if (ImGui::Combo("##runA", &runAIndex, getter, &labels, static_cast<int>(labels.size())))
            RefreshData();
        ImGui::Text(ICON_FA_CLOCK_ROTATE_LEFT "  Прогон B (сравнение)");
        if (ImGui::Combo("##runB", &runBIndex, getter, &labels, static_cast<int>(labels.size())))
            RefreshData();

        int64_t runAId = runs[static_cast<size_t>(runAIndex)].id;
        int64_t runBId = runs[static_cast<size_t>(runBIndex)].id;
        if (app->RulesOutOfDate(runAId) || app->RulesOutOfDate(runBId)) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                               "%s  Правила изменились — отметки пересчитаны", ICON_FA_ROTATE);
            if (app->RulesOutOfDate(runAId)) app->ReapplyRules(runAId);
            if (app->RulesOutOfDate(runBId)) app->ReapplyRules(runBId);
            RefreshData();
        }

        ImGui::Separator();
        ImGui::Text("В обоих: %d (изменились: %d) · только в A: %d · только в B: %d",
                    result.both, result.changed, result.onlyA, result.onlyB);
        ImGui::Text("Отмечено A: %d (%s ₽) · B: %d (%s ₽) · динамика: %s ₽",
                    result.flaggedCountA, FormatAmount(result.flaggedA).c_str(),
                    result.flaggedCountB, FormatAmount(result.flaggedB).c_str(),
                    FormatAmount(result.flaggedB - result.flaggedA).c_str());

        ImGui::Separator();
        if (ImGui::BeginTabBar("compare_tabs")) {
            if (ImGui::BeginTabItem(ICON_FA_CODE_COMPARE "  Позиции")) {
                tab = 0;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_LAYER_GROUP "  Субсчёты")) {
                tab = 1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_GAVEL "  Правила")) {
                tab = 2;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (tab == 0) {
            InputTextSync("Поиск по названию", filter, 200.0f);
            ImGui::SameLine();
            ImGui::Checkbox(ICON_FA_FILTER "  Только изменившиеся", &onlyChanged);

            std::vector<const CompareRow*> shown;
            for (const CompareRow& row : result.rows) {
                if (onlyChanged && !(row.inA && row.inB && std::fabs(row.closeA - row.closeB) > 0.01))
                    continue;
                if (!filter.empty() && !ContainsUtf8(row.nameRaw, filter)) continue;
                shown.push_back(&row);
            }

            if (ImGui::BeginTable("compare_table", 7,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Номенклатура", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Остаток A", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Остаток B", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Δ", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Отмечено A", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Отмечено B", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();
                int rowId = 1;
                for (const CompareRow* row : shown) {
                    ImGui::TableNextRow();
                    ImGui::PushID(rowId);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", rowId);
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", row->nameRaw.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(row->closeA).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(row->closeB).c_str());
                    ImGui::TableNextColumn();
                    double delta = row->closeB - row->closeA;
                    if (delta > 0.01) ImGui::TextColored(ImVec4(0.10f, 0.60f, 0.30f, 1.0f), "%s", FormatAmount(delta).c_str());
                    else if (delta < -0.01) ImGui::TextColored(ImVec4(0.85f, 0.20f, 0.20f, 1.0f), "%s", FormatAmount(delta).c_str());
                    else ImGui::TextDisabled("—");
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", row->flaggedA ? row->ruleA.c_str() : "");
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", row->flaggedB ? row->ruleB.c_str() : "");
                    ImGui::PopID();
                    ++rowId;
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Показано %zu из %zu", shown.size(), result.rows.size());
        } else if (tab == 1) {
            if (ImGui::BeginTable("compare_subs", 8,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Субсчёт", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Позиции A", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Позиции B", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Остаток A", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Остаток B", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Отмечено A", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Отмечено B", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                int rowId = 1;
                for (const SubaccountCompare& sub : subRows) {
                    ImGui::TableNextRow();
                    ImGui::PushID(rowId);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", rowId);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", sub.code.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", sub.itemsA);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", sub.itemsB);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(sub.balanceA).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(sub.balanceB).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(sub.flaggedA).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(sub.flaggedB).c_str());
                    ImGui::PopID();
                    ++rowId;
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Субсчётов: %zu", subRows.size());
        } else {
            if (ImGui::BeginTable("compare_rules", 7,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Сумма A", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Сумма B", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Δ", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Пометок A/B", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();
                int rowId = 1;
                for (const RuleCompare& rule : ruleRows) {
                    ImGui::TableNextRow();
                    ImGui::PushID(rowId);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", rowId);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", rule.ruleCode.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", rule.category.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(rule.amountA).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(rule.amountB).c_str());
                    ImGui::TableNextColumn();
                    double delta = rule.amountB - rule.amountA;
                    if (delta > 0.01) ImGui::TextColored(ImVec4(0.10f, 0.60f, 0.30f, 1.0f), "%s", FormatAmount(delta).c_str());
                    else if (delta < -0.01) ImGui::TextColored(ImVec4(0.85f, 0.20f, 0.20f, 1.0f), "%s", FormatAmount(delta).c_str());
                    else ImGui::TextDisabled("—");
                    ImGui::TableNextColumn();
                    ImGui::Text("%d / %d", rule.countA, rule.countB);
                    ImGui::PopID();
                    ++rowId;
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Правил с отметками: %zu", ruleRows.size());
        }

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DOWNLOAD "  Экспорт CSV")) {
            std::string path = ExportCsv();
            if (path.empty()) app->ShowError("Не удалось записать CSV");
            else app->SetStatus("CSV сохранён: " + path);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_LINES "  HTML-сравнение")) {
            std::string path = ExportHtml();
            if (path.empty()) app->ShowError("Не удалось записать HTML");
            else {
                lastExport = path;
                app->SetStatus("HTML сохранён: " + path);
            }
        }
        if (!lastExport.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_GLOBE "  Открыть в браузере")) {
                std::string command = "xdg-open " + lastExport + " 2>/dev/null";
                if (std::system(command.c_str()) != 0)
                    app->ShowError("Не удалось открыть браузер: " + lastExport);
            }
        }

        EndWindow();
    }
}
