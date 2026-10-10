#include "ReportView.h"

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
#include <map>
#include <sstream>
#include <unordered_map>

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

void ReportView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    runs = app->Db().ListRuns("", "", "", false);
    if (runIndex < 0 || runIndex >= static_cast<int>(runs.size())) runIndex = 0;
    if (!runs.empty()) {
        items = app->Db().ListRunItems(runs[static_cast<size_t>(runIndex)].id);
        flags = app->Db().ListRunFlags(runs[static_cast<size_t>(runIndex)].id);
    }
}

std::string ReportView::ExportCsv() {
    std::string path = ReportDirectory() + "/run_" + std::to_string(runs[static_cast<size_t>(runIndex)].id) +
                       "_" + NowStamp() + ".csv";
    std::ofstream out(path);
    if (!out) return {};

    out << "row,name,subaccount,opening,debit,credit,closing,qty_close,unit_price,rule,category,severity,amount,rationale\n";
    std::unordered_map<int64_t, std::vector<Flag>> byItem;
    for (const Flag& flag : flags) byItem[flag.itemId].push_back(flag);

    for (const ItemRecord& rec : items) {
        auto it = byItem.find(rec.id);
        if (it == byItem.end()) {
            out << rec.item.rowNo << "," << CsvEscape(rec.item.nameRaw) << "," << CsvEscape(rec.subaccount)
                << "," << rec.item.openDebit - rec.item.openCredit << "," << rec.item.inDebit << ","
                << rec.item.outCredit << "," << rec.item.closeDebit - rec.item.closeCredit << ","
                << rec.item.qtyClose << "," << rec.item.unitPrice << ",,,0,0,\"\"\n";
            continue;
        }
        for (const Flag& flag : it->second) {
            out << rec.item.rowNo << "," << CsvEscape(rec.item.nameRaw) << "," << CsvEscape(rec.subaccount)
                << "," << rec.item.openDebit - rec.item.openCredit << "," << rec.item.inDebit << ","
                << rec.item.outCredit << "," << rec.item.closeDebit - rec.item.closeCredit << ","
                << rec.item.qtyClose << "," << rec.item.unitPrice << "," << CsvEscape(flag.ruleCode) << ","
                << CsvEscape(flag.category) << "," << flag.severity << "," << flag.amount << ","
                << CsvEscape(flag.rationale) << "\n";
        }
    }
    return path;
}

std::string ReportView::ExportHtml() {
    const Run& run = runs[static_cast<size_t>(runIndex)];
    std::string path = ReportDirectory() + "/run_" + std::to_string(run.id) + "_" + NowStamp() + ".html";
    std::ofstream out(path);
    if (!out) return {};

    std::unordered_map<std::string, std::pair<int, double>> byCategory;
    std::unordered_map<std::string, std::pair<int, double>> bySubaccount;
    std::unordered_map<std::string, std::pair<int, double>> byRule;
    double totalFlagged = 0.0;
    for (const Flag& flag : flags) {
        byCategory[flag.category].first += 1;
        byCategory[flag.category].second += flag.amount;
        byRule[flag.ruleCode].first += 1;
        byRule[flag.ruleCode].second += flag.amount;
        totalFlagged += flag.amount;
    }
    for (const ItemRecord& rec : items) {
        bySubaccount[rec.subaccount].first += 1;
        bySubaccount[rec.subaccount].second += rec.item.closeDebit - rec.item.closeCredit;
    }

    out << "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>Отчёт по прогону "
        << run.id << "</title><style>body{font-family:sans-serif;margin:24px}"
        "table{border-collapse:collapse;margin:12px 0}th,td{border:1px solid #999;padding:6px 10px}"
        "th{background:#e8eef3}h1{font-size:20px}</style></head><body>";
    out << "<h1>Аудит счёта 105 — прогон #" << run.id << "</h1>";
    out << "<p>Файл: " << HtmlEscape(run.file) << " · формат: " << HtmlEscape(run.format)
        << " · учреждение: " << HtmlEscape(run.organization) << " · период: " << HtmlEscape(run.period)
        << " · импорт: " << HtmlEscape(run.importedAt) << "</p>";
    out << "<p>Позиций: " << run.items << " · отмечено: " << run.flagged
        << " · сумма отмеченных: " << FormatAmount(totalFlagged) << " ₽</p>";

    out << "<h2>По категориям</h2><table><tr><th>Категория</th><th>Позиций</th><th>Сумма</th></tr>";
    for (const auto& entry : byCategory) {
        out << "<tr><td>" << HtmlEscape(entry.first) << "</td><td>" << entry.second.first
            << "</td><td>" << FormatAmount(entry.second.second) << "</td></tr>";
    }
    out << "</table>";

    out << "<h2>По правилам</h2><table><tr><th>Правило</th><th>Позиций</th><th>Сумма</th></tr>";
    for (const auto& entry : byRule) {
        out << "<tr><td>" << HtmlEscape(entry.first) << "</td><td>" << entry.second.first
            << "</td><td>" << FormatAmount(entry.second.second) << "</td></tr>";
    }
    out << "</table>";

    out << "<h2>По субсчётам</h2><table><tr><th>Субсчёт</th><th>Позиций</th><th>Остаток</th></tr>";
    for (const auto& entry : bySubaccount) {
        out << "<tr><td>" << HtmlEscape(entry.first) << "</td><td>" << entry.second.first
            << "</td><td>" << FormatAmount(entry.second.second) << "</td></tr>";
    }
    out << "</table>";

    out << "<h2>Реестр помеченных позиций (топ-50)</h2><table><tr><th>Стр.</th><th>Номенклатура</th>"
        "<th>Субсчёт</th><th>Остаток</th><th>Правило</th><th>Категория</th><th>Основание</th></tr>";
    std::vector<ItemRecord> flaggedItems;
    for (const ItemRecord& rec : items) {
        if (!rec.ruleCode.empty()) flaggedItems.push_back(rec);
    }
    std::sort(flaggedItems.begin(), flaggedItems.end(), [](const ItemRecord& a, const ItemRecord& b) {
        return a.item.closeDebit - a.item.closeCredit > b.item.closeDebit - b.item.closeCredit;
    });
    std::unordered_map<int64_t, std::string> rationaleByItem;
    for (const Flag& flag : flags) {
        if (!rationaleByItem.count(flag.itemId)) rationaleByItem[flag.itemId] = flag.rationale;
    }
    for (size_t i = 0; i < std::min<size_t>(50, flaggedItems.size()); ++i) {
        const ItemRecord& rec = flaggedItems[i];
        out << "<tr><td>" << rec.item.rowNo << "</td><td>" << HtmlEscape(rec.item.nameRaw) << "</td><td>"
            << HtmlEscape(rec.subaccount) << "</td><td>" << FormatAmount(rec.item.closeDebit - rec.item.closeCredit)
            << "</td><td>" << HtmlEscape(rec.ruleCode) << "</td><td>" << HtmlEscape(rec.category)
            << "</td><td>" << HtmlEscape(rationaleByItem[rec.id]) << "</td></tr>";
    }
    out << "</table></body></html>";
    return path;
}

void ReportView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }
        if (runs.empty()) RefreshData();
        if (runs.empty()) {
            ImGui::TextDisabled("Нет прогонов для отчёта.");
            EndWindow();
            return;
        }

        std::vector<std::string> labels;
        for (const Run& run : runs) {
            std::string label = "#" + std::to_string(run.id) + "  " + run.file;
            if (!run.period.empty()) label += " | " + run.period;
            labels.push_back(label);
        }
        auto getter = [](void* data, int idx) -> const char* {
            return (*reinterpret_cast<std::vector<std::string>*>(data))[idx].c_str();
        };
        if (ImGui::Combo(ICON_FA_DATABASE "  Прогон", &runIndex, getter, &labels,
                         static_cast<int>(labels.size()))) {
            RefreshData();
        }

        const Run& run = runs[static_cast<size_t>(runIndex)];
        if (app->RulesOutOfDate(run.id)) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                               "%s  Правила изменились — отметки пересчитаны", ICON_FA_ROTATE);
            app->ReapplyRules(run.id);
            RefreshData();
        }
        ImGui::Separator();
        ImGui::Text("Позиций: %d · отмечено: %d · примечаний: %d", run.items, run.flagged, run.noted);

        std::map<std::string, std::pair<int, double>> byCategory;
        for (const Flag& flag : flags) {
            byCategory[flag.category].first += 1;
            byCategory[flag.category].second += flag.amount;
        }
        if (ImGui::BeginTable("report_categories", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Позиций", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            int rowId = 1;
            for (const auto& entry : byCategory) {
                ImGui::TableNextRow();
                ImGui::PushID(rowId);
                ImGui::TableNextColumn();
                ImGui::Text("%d", rowId);
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.first.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", entry.second.first);
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(entry.second.second).c_str());
                ImGui::PopID();
                ++rowId;
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_FILE_LINES "  Реестр помеченных позиций (топ-50)");
        std::vector<ItemRecord> flaggedItems;
        for (const ItemRecord& rec : items) {
            if (!rec.ruleCode.empty()) flaggedItems.push_back(rec);
        }
        std::sort(flaggedItems.begin(), flaggedItems.end(), [](const ItemRecord& a, const ItemRecord& b) {
            return a.item.closeDebit - a.item.closeCredit > b.item.closeDebit - b.item.closeCredit;
        });
        if (ImGui::BeginTable("report_registry", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Стр.", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Номенклатура", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Субсчёт", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Остаток", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < std::min<size_t>(50, flaggedItems.size()); ++i) {
                const ItemRecord& rec = flaggedItems[i];
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
                ImGui::TableNextColumn();
                ImGui::Text("%s", rec.ruleCode.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DOWNLOAD "  Экспорт CSV")) {
            std::string path = ExportCsv();
            if (path.empty()) app->ShowError("Не удалось записать CSV");
            else app->SetStatus("CSV сохранён: " + path);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_LINES "  HTML-сводка")) {
            std::string path = ExportHtml();
            if (path.empty()) app->ShowError("Не удалось записать HTML");
            else {
                lastExport = path;
                app->SetStatus("HTML сохранён: " + path);
            }
        }
        if (!lastExport.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_PRINT "  Печать (lp)")) {
                std::string command = "lp " + lastExport + " 2>/dev/null";
                if (std::system(command.c_str()) == 0) app->SetStatus("Отчёт отправлен на печать");
                else app->ShowError("Печать недоступна: проверьте, что установлен CUPS (lp)");
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_GLOBE "  Открыть в браузере")) {
                std::string command = "xdg-open " + lastExport + " 2>/dev/null";
                if (std::system(command.c_str()) != 0) {
                    app->ShowError("Не удалось открыть браузер: " + lastExport);
                }
            }
        }

        EndWindow();
    }
}
