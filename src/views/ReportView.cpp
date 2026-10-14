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
#include <unordered_set>

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

double CloseBalance(const ItemRecord& rec) {
    return rec.item.closeDebit - rec.item.closeCredit;
}

void SortByBalance(std::vector<ItemRecord>& rows) {
    std::sort(rows.begin(), rows.end(), [](const ItemRecord& a, const ItemRecord& b) {
        return CloseBalance(a) > CloseBalance(b);
    });
}

}  // namespace

void ReportView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    runs = app->Db().ListRuns("", "", "", false);
    if (runIndex < 0 || runIndex >= static_cast<int>(runs.size())) runIndex = 0;
    categories = app->Db().ListCategories();
    categoryColor.clear();
    categoryHex.clear();
    for (const Category& cat : categories) {
        categoryColor[cat.name] = ParseHexColor(cat.color);
        categoryHex[cat.name] = NormalizeHexColor(cat.color);
    }
    if (!runs.empty()) {
        items = app->Db().ListRunItems(runs[static_cast<size_t>(runIndex)].id);
        flags = app->Db().ListRunFlags(runs[static_cast<size_t>(runIndex)].id);
    }

    std::unordered_map<std::string, int> seen;
    for (const Flag& flag : flags) ++seen[flag.ruleCode];
    for (const ItemRecord& rec : items) {
        if (!rec.ruleCode.empty()) ++seen[rec.ruleCode];
    }
    ruleCodes.clear();
    ruleCodes.reserve(seen.size());
    for (const auto& entry : seen) ruleCodes.push_back(entry.first);
    std::sort(ruleCodes.begin(), ruleCodes.end());
    ruleSelected.assign(ruleCodes.size(), 1);
}

void ReportView::SelectRules(const std::vector<std::string>& codes) {
    if (codes.empty()) {
        ruleSelected.assign(ruleCodes.size(), 1);
        return;
    }
    ruleSelected.assign(ruleCodes.size(), 0);
    for (size_t i = 0; i < ruleCodes.size(); ++i) {
        for (const std::string& code : codes) {
            if (ruleCodes[i] == code) {
                ruleSelected[i] = 1;
                break;
            }
        }
    }
}

std::vector<ItemRecord> ReportView::FilteredRows() const {
    std::unordered_set<std::string> selected;
    for (size_t i = 0; i < ruleCodes.size(); ++i) {
        if (ruleSelected.empty() || ruleSelected[i]) selected.insert(ruleCodes[i]);
    }

    std::unordered_set<int64_t> matchedItems;
    if (!ruleCodes.empty()) {
        for (const Flag& flag : flags) {
            if (selected.count(flag.ruleCode)) matchedItems.insert(flag.itemId);
        }
    }

    const std::string needle = ToLowerUtf8(search);

    std::vector<ItemRecord> out;
    for (const ItemRecord& rec : items) {
        if (onlyFlagged && rec.ruleCode.empty()) continue;
        if (!ruleCodes.empty() && !matchedItems.count(rec.id)) continue;
        if (!categoryFilter.empty() && rec.category != categoryFilter) continue;
        if (!subFilter.empty() && !ContainsUtf8(rec.subaccount, subFilter)) continue;
        if (minSum > 0.0 && CloseBalance(rec) < minSum) continue;
        if (!needle.empty() && !ContainsUtf8(rec.item.nameNorm, needle)) continue;
        out.push_back(rec);
    }
    SortByBalance(out);
    return out;
}

std::vector<Flag> ReportView::FilteredFlags() const {
    std::unordered_set<std::string> selected;
    for (size_t i = 0; i < ruleCodes.size(); ++i) {
        if (ruleSelected.empty() || ruleSelected[i]) selected.insert(ruleCodes[i]);
    }

    std::unordered_set<int64_t> shownIds;
    for (const ItemRecord& rec : FilteredRows()) shownIds.insert(rec.id);

    std::vector<Flag> out;
    for (const Flag& flag : flags) {
        if (!shownIds.count(flag.itemId)) continue;
        if (!ruleCodes.empty() && !selected.count(flag.ruleCode)) continue;
        out.push_back(flag);
    }
    return out;
}

std::string ReportView::FilterLabel() const {
    int selectedCount = 0;
    for (size_t i = 0; i < ruleCodes.size(); ++i) {
        if (ruleSelected.empty() || ruleSelected[i]) ++selectedCount;
    }
    std::ostringstream out;
    if (ruleCodes.empty()) {
        out << "правил нет";
    } else if (selectedCount == static_cast<int>(ruleCodes.size())) {
        out << "все правила (" << ruleCodes.size() << ")";
    } else if (selectedCount == 0) {
        out << "правила не выбраны";
    } else {
        out << "правила: ";
        bool first = true;
        for (size_t i = 0; i < ruleCodes.size(); ++i) {
            if (!ruleSelected[i]) continue;
            if (!first) out << ", ";
            out << ruleCodes[i];
            first = false;
        }
    }
    if (!categoryFilter.empty()) out << " · категория: " << categoryFilter;
    if (!subFilter.empty()) out << " · субсчёт: " << subFilter;
    if (minSum > 0.0) out << " · остаток ≥ " << FormatAmount(minSum);
    if (!search.empty()) out << " · поиск: " << search;
    return out.str();
}

std::string ReportView::ExportCsv() {
    const Run& run = runs[static_cast<size_t>(runIndex)];
    std::string path = ReportDirectory() + "/run_" + std::to_string(run.id) +
                       "_" + NowStamp() + ".csv";
    std::ofstream out(path);
    if (!out) return {};

    std::vector<ItemRecord> rows = FilteredRows();
    std::vector<Flag> filteredFlags = FilteredFlags();

    out << "# Прогон #" << run.id << " | " << run.file << " | период: " << run.period
        << " | фильтр: " << FilterLabel() << " | строк: " << rows.size() << "\n";
    out << "row,name,subaccount,opening,debit,credit,closing,qty_close,unit_price,rule,category,severity,amount,rationale\n";

    std::unordered_map<int64_t, std::vector<Flag>> byItem;
    for (const Flag& flag : filteredFlags) byItem[flag.itemId].push_back(flag);

    for (const ItemRecord& rec : rows) {
        auto it = byItem.find(rec.id);
        if (it == byItem.end()) {
            out << rec.item.rowNo << "," << CsvEscape(rec.item.nameRaw) << "," << CsvEscape(rec.subaccount)
                << "," << rec.item.openDebit - rec.item.openCredit << "," << rec.item.inDebit << ","
                << rec.item.outCredit << "," << CloseBalance(rec) << "," << rec.item.qtyClose << ","
                << rec.item.unitPrice << ",,,0,0,\"\"\n";
            continue;
        }
        for (const Flag& flag : it->second) {
            out << rec.item.rowNo << "," << CsvEscape(rec.item.nameRaw) << "," << CsvEscape(rec.subaccount)
                << "," << rec.item.openDebit - rec.item.openCredit << "," << rec.item.inDebit << ","
                << rec.item.outCredit << "," << CloseBalance(rec) << "," << rec.item.qtyClose << ","
                << rec.item.unitPrice << "," << CsvEscape(flag.ruleCode) << "," << CsvEscape(flag.category)
                << "," << flag.severity << "," << flag.amount << "," << CsvEscape(flag.rationale) << "\n";
        }
    }
    return path;
}

std::string ReportView::ExportHtml() {
    const Run& run = runs[static_cast<size_t>(runIndex)];
    std::string path = ReportDirectory() + "/run_" + std::to_string(run.id) + "_" + NowStamp() + ".html";
    std::ofstream out(path);
    if (!out) return {};

    std::vector<ItemRecord> rows = FilteredRows();
    std::vector<Flag> filteredFlags = FilteredFlags();

    std::map<std::string, std::pair<int, double>> byCategory;
    std::map<std::string, std::pair<int, double>> bySubaccount;
    std::map<std::string, std::pair<int, double>> byRule;
    double totalFlagged = 0.0;
    for (const Flag& flag : filteredFlags) {
        byCategory[flag.category].first += 1;
        byCategory[flag.category].second += flag.amount;
        byRule[flag.ruleCode].first += 1;
        byRule[flag.ruleCode].second += flag.amount;
        totalFlagged += flag.amount;
    }
    for (const ItemRecord& rec : rows) {
        bySubaccount[rec.subaccount].first += 1;
        bySubaccount[rec.subaccount].second += CloseBalance(rec);
    }

    out << "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>Отчёт по прогону "
        << run.id << "</title><style>body{font-family:sans-serif;margin:24px}"
        "table{border-collapse:collapse;margin:12px 0}th,td{border:1px solid #999;padding:6px 10px}"
        "th{background:#e8eef3}h1{font-size:20px}</style></head><body>";
    out << "<h1>Аудит счёта 105 — прогон #" << run.id << "</h1>";
    out << "<p>Файл: " << HtmlEscape(run.file) << " · формат: " << HtmlEscape(run.format)
        << " · учреждение: " << HtmlEscape(run.organization) << " · период: " << HtmlEscape(run.period)
        << " · импорт: " << HtmlEscape(run.importedAt) << "</p>";
    out << "<p>Фильтр: " << HtmlEscape(FilterLabel()) << "</p>";
    out << "<p>Позиций всего: " << run.items << " · в выборке: " << rows.size()
        << " · отмечено: " << run.flagged << " · сумма отмеченных: " << FormatAmount(totalFlagged)
        << " ₽</p>";

    out << "<h2>По категориям</h2><table><tr><th>Категория</th><th>Позиций</th><th>Сумма</th></tr>";
    for (const auto& entry : byCategory) {
        std::string hex = categoryHex.count(entry.first) ? categoryHex[entry.first] : "#e8eef3";
        out << "<tr style=\"background:" << hex + "22\"><td>" << HtmlEscape(entry.first)
            << "</td><td>" << entry.second.first << "</td><td>" << FormatAmount(entry.second.second)
            << "</td></tr>";
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

    size_t shown = rowLimit < 0 ? rows.size() : std::min<size_t>(static_cast<size_t>(rowLimit), rows.size());
    out << "<h2>Реестр позиций (" << shown << " из " << rows.size() << ")</h2><table><tr><th>Стр.</th>"
        "<th>Номенклатура</th><th>Субсчёт</th><th>Остаток</th><th>Правило</th><th>Категория</th>"
        "<th>Сумма</th><th>Основание</th></tr>";
    std::unordered_map<int64_t, std::string> rationaleByItem;
    std::unordered_map<int64_t, double> amountByItem;
    std::unordered_map<int64_t, std::string> rulesByItem;
    for (const Flag& flag : filteredFlags) {
        if (!rationaleByItem.count(flag.itemId)) rationaleByItem[flag.itemId] = flag.rationale;
        amountByItem[flag.itemId] += flag.amount;
        if (!rulesByItem[flag.itemId].empty()) rulesByItem[flag.itemId] += ", ";
        rulesByItem[flag.itemId] += flag.ruleCode;
    }
    for (size_t i = 0; i < shown; ++i) {
        const ItemRecord& rec = rows[i];
        std::string hex = categoryHex.count(rec.category) ? categoryHex[rec.category] : "";
        const std::string codes = rulesByItem.count(rec.id) ? rulesByItem[rec.id] : rec.ruleCode;
        out << "<tr" << (hex.empty() ? "" : " style=\"background:" + hex + "22\"")
            << "><td>" << rec.item.rowNo << "</td><td>" << HtmlEscape(rec.item.nameRaw) << "</td><td>"
            << HtmlEscape(rec.subaccount) << "</td><td>" << FormatAmount(CloseBalance(rec)) << "</td><td>"
            << HtmlEscape(codes) << "</td><td>" << HtmlEscape(rec.category) << "</td><td>"
            << FormatAmount(amountByItem[rec.id]) << "</td><td>"
            << HtmlEscape(rationaleByItem.count(rec.id) ? rationaleByItem[rec.id] : std::string())
            << "</td></tr>";
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

        ImGui::Separator();
        ImGui::Text(ICON_FA_FILTER "  Фильтр строк (применяется и к экспорту)");
        InputTextSync(ICON_FA_MAGNIFYING_GLASS "  Поиск", search);
        ImGui::SameLine();
        InputTextSync("Субсчёт", subFilter);
        ImGui::SameLine();
        AmountInput("Мин. остаток", minSum);
        ImGui::SameLine();
        ImGui::Checkbox("Только отмеченные", &onlyFlagged);

        std::vector<std::string> catNames;
        catNames.push_back("Все категории");
        for (const Category& cat : categories) catNames.push_back(cat.name);
        int catIndex = 0;
        for (size_t i = 1; i < catNames.size(); ++i) {
            if (catNames[i] == categoryFilter) catIndex = static_cast<int>(i);
        }
        ImGui::SetNextItemWidth(220.0f);
        auto catGetter = [](void* data, int idx) -> const char* {
            return (*reinterpret_cast<std::vector<std::string>*>(data))[idx].c_str();
        };
        if (ImGui::Combo(ICON_FA_TAGS "  Категория", &catIndex, catGetter, &catNames,
                         static_cast<int>(catNames.size()))) {
            categoryFilter = catIndex > 0 ? catNames[static_cast<size_t>(catIndex)] : std::string();
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_GAVEL "  Правила прогона (%zu)", ruleCodes.size());
        if (ruleCodes.empty()) {
            ImGui::TextDisabled("Ни одно правило не сработало в этом прогоне.");
        } else {
            std::unordered_map<std::string, int> ruleCount;
            std::unordered_map<std::string, double> ruleAmount;
            std::unordered_map<std::string, std::string> ruleCategory;
            for (const Flag& flag : flags) {
                ++ruleCount[flag.ruleCode];
                ruleAmount[flag.ruleCode] += flag.amount;
                if (ruleCategory[flag.ruleCode].empty()) ruleCategory[flag.ruleCode] = flag.category;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
            if (ImGui::BeginTable("report_rules", 5,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Позиций", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < ruleCodes.size(); ++i) {
                    const std::string& code = ruleCodes[i];
                    ImGui::TableNextRow();
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextColumn();
                    bool selected = ruleSelected.empty() || ruleSelected[i] != 0;
                    if (ImGui::Checkbox(code.c_str(), &selected)) {
                        ruleSelected[i] = selected ? 1 : 0;
                    }
                    ImGui::TableNextColumn();
                    auto it = categoryColor.find(ruleCategory[code]);
                    ImVec4 color = it != categoryColor.end() ? it->second : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    ImGui::TextColored(color, "%s", ruleCategory[code].c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", ruleCount.count(code) ? ruleCount[code] : 0);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(ruleAmount[code]).c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            if (ImGui::Button(ICON_FA_CHECK "  Все правила")) {
                ruleSelected.assign(ruleCodes.size(), 1);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "  Снять выбор")) {
                ruleSelected.assign(ruleCodes.size(), 0);
            }
            ImGui::TextDisabled("Строка попадает в выборку, если хотя бы одно её правило отмечено.");
        }

        std::vector<ItemRecord> rows = FilteredRows();
        std::vector<Flag> filteredFlags = FilteredFlags();
        ImGui::TextDisabled("Фильтр: %s · в выборке: %zu позиций · отметок: %zu",
                            FilterLabel().c_str(), rows.size(), filteredFlags.size());

        std::map<std::string, std::pair<int, double>> byCategory;
        for (const Flag& flag : filteredFlags) {
            byCategory[flag.category].first += 1;
            byCategory[flag.category].second += flag.amount;
        }
        if (!byCategory.empty()) {
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
                    auto it = categoryColor.find(entry.first);
                    if (it != categoryColor.end()) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                              IM_COL32(static_cast<int>(it->second.x * 255),
                                                      static_cast<int>(it->second.y * 255),
                                                      static_cast<int>(it->second.z * 255), 90));
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", rowId);
                    ImGui::TableNextColumn();
                    if (it != categoryColor.end()) ImGui::TextColored(it->second, "%s", entry.first.c_str());
                    else ImGui::Text("%s", entry.first.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", entry.second.first);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", FormatAmount(entry.second.second).c_str());
                    ImGui::PopID();
                    ++rowId;
                }
                ImGui::EndTable();
            }
        }

        ImGui::Separator();
        const char* limits[] = {"50", "100", "200", "500", "все"};
        int limitIndex = 2;
        if (rowLimit == 50) limitIndex = 0;
        else if (rowLimit == 100) limitIndex = 1;
        else if (rowLimit == 500) limitIndex = 3;
        else if (rowLimit < 0) limitIndex = 4;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Показывать строк", &limitIndex, limits, 5)) {
            rowLimit = limitIndex == 4 ? -1 : std::atoi(limits[limitIndex]);
        }

        ImGui::Text(ICON_FA_FILE_LINES "  Реестр позиций по фильтру");
        size_t shownRows = rowLimit < 0 ? rows.size()
                                        : std::min<size_t>(static_cast<size_t>(rowLimit), rows.size());
        if (ImGui::BeginTable("report_registry", 9,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Стр.", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Номенклатура", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Субсчёт", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Остаток", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Основание", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            std::unordered_map<int64_t, double> amountByItem;
            std::unordered_map<int64_t, std::string> rationaleByItem;
            std::unordered_map<int64_t, std::string> rulesByItem;
            for (const Flag& flag : filteredFlags) {
                amountByItem[flag.itemId] += flag.amount;
                if (!rationaleByItem.count(flag.itemId)) rationaleByItem[flag.itemId] = flag.rationale;
                if (!rulesByItem[flag.itemId].empty()) rulesByItem[flag.itemId] += ", ";
                rulesByItem[flag.itemId] += flag.ruleCode;
            }

            for (size_t i = 0; i < shownRows; ++i) {
                const ItemRecord& rec = rows[i];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(rec.id));
                auto it = categoryColor.find(rec.category);
                if (!rec.item.arithOk) {
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
                ImGui::Text("%d", rec.item.rowNo);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", rec.item.nameRaw.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", rec.subaccount.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(CloseBalance(rec)).c_str());
                ImGui::TableNextColumn();
                const std::string codes = rulesByItem.count(rec.id) ? rulesByItem[rec.id] : rec.ruleCode;
                if (it != categoryColor.end()) ImGui::TextColored(it->second, "%s", codes.c_str());
                else ImGui::Text("%s", codes.c_str());
                ImGui::TableNextColumn();
                if (it != categoryColor.end()) ImGui::TextColored(it->second, "%s", rec.category.c_str());
                else ImGui::Text("%s", rec.category.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(amountByItem[rec.id]).c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", rationaleByItem[rec.id].c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Показано %zu из %zu. Легенда: фон строки = цвет категории, "
                            "красный — арифметика не сходится.", shownRows, rows.size());

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DOWNLOAD "  Экспорт CSV (по фильтру)")) {
            std::string path = ExportCsv();
            if (path.empty()) app->ShowError("Не удалось записать CSV");
            else app->SetStatus("CSV сохранён: " + path);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_LINES "  HTML-сводка (по фильтру)")) {
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
