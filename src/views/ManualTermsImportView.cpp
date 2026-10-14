#include "ManualTermsImportView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../ImGuiFileDialog.h"
#include "../Widgets.h"
#include "../domain/CsvReader.h"
#include "../domain/Spreadsheet.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int FindCsvColumn(const std::vector<std::string>& headers, const std::vector<const char*>& aliases) {
    for (size_t h = 0; h < headers.size(); ++h) {
        for (const char* alias : aliases) {
            if (headers[h] == alias) return static_cast<int>(h);
        }
    }
    for (size_t h = 0; h < headers.size(); ++h) {
        for (const char* alias : aliases) {
            if (headers[h].find(alias) != std::string::npos) return static_cast<int>(h);
        }
    }
    return -1;
}

} // namespace

void ManualTermsImportView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    rules = app->Db().ListRules();
}

std::string ManualTermsImportView::ProfileToRuleCode(const std::string& profile) const {
    std::string p = ToLowerUtf8(TrimUtf8(profile));
    if (p.empty()) return "";
    if (p.rfind("oms", 0) == 0) {
        for (char& c : p) c = (char)std::toupper((unsigned char)c);
        return p;
    }
    auto contains = [&](const char* needle) { return p.find(needle) != std::string::npos; };
    if (contains("вакцин")) return "OMS01";
    if (contains("вич") || contains("спид") || contains("арт") || contains("антиретров")) return "OMS02";
    if (contains("туберк")) return "OMS03";
    if (contains("нарк") || contains("зависим") || contains("алког") || contains("дисульф") ||
        contains("налтрекс") || contains("метадон")) {
        return "OMS04";
    }
    if (contains("тест") || contains("ивд") || contains("скрин") || contains("гепатит")) return "OMS05";
    return "";
}

void ManualTermsImportView::ParseCsvTerms() {
    csvRows.clear();
    csvBadRows = 0;
    csvImported = 0;
    csvUpdated = 0;
    csvConflicts = 0;
    csvSkipped = 0;
    csvError.clear();
    csvParsed = false;
    csvParseAttempted = true;

    if (csvPath.empty()) {
        csvError = "Файл не выбран";
        return;
    }

    Spreadsheet sheet;
    std::string error;
    if (!ReadCsv(csvPath, sheet, error)) {
        csvError = error.empty() ? "Не удалось прочитать CSV" : error;
        return;
    }

    int headerRow = -1;
    for (size_t r = 0; r < sheet.rows.size(); ++r) {
        bool any = false;
        for (const auto& cell : sheet.rows[r].cells) {
            if (!TrimUtf8(cell.text).empty()) { any = true; break; }
        }
        if (any) { headerRow = static_cast<int>(r); break; }
    }
    if (headerRow < 0 || headerRow + 1 >= (int)sheet.rows.size()) {
        csvError = "В CSV нет заголовка или строк данных";
        return;
    }

    std::vector<std::string> headers;
    for (const auto& cell : sheet.rows[headerRow].cells) {
        headers.push_back(ToLowerUtf8(TrimUtf8(cell.text)));
    }

    int colTerm = FindCsvColumn(headers, {"term", "термин", "наименование", "наменование", "name", "номенклатура", "лс", "наименов"});
    int colProfile = FindCsvColumn(headers, {"profile", "профиль", "категория", "indication", "показание"});
    int colRule = FindCsvColumn(headers, {"rule_code", "rule", "правило", "код правила", "код"});
    int colList = FindCsvColumn(headers, {"list_name", "перечень", "список", "лист"});
    int colSource = FindCsvColumn(headers, {"source_doc", "источник", "источник данных"});
    int colDate = FindCsvColumn(headers, {"as_of_date", "дата", "дата актуальности"});
    int colScope = FindCsvColumn(headers, {"scope", "область"});
    int colActive = FindCsvColumn(headers, {"active", "активен", "активность"});

    if (colTerm < 0) {
        csvError = "Не найдена обязательная колонка term/наименование";
        return;
    }

    auto cellText = [&](const Row& row, int col) -> std::string {
        if (col < 0) return std::string();
        if (col >= (int)row.cells.size()) return std::string();
        return TrimUtf8(row.cells[col].text);
    };

    for (size_t r = (size_t)headerRow + 1; r < sheet.rows.size(); ++r) {
        const Row& row = sheet.rows[r];
        CsvTermRow item;
        item.term = cellText(row, colTerm);
        if (item.term.empty()) continue;

        item.profile = cellText(row, colProfile);
        std::string explicitRule = cellText(row, colRule);
        item.ruleCode = explicitRule.empty() ? item.profile : explicitRule;
        item.listName = cellText(row, colList);
        item.sourceDoc = cellText(row, colSource);
        item.asOfDate = cellText(row, colDate);
        item.scope = cellText(row, colScope);
        if (item.scope.empty()) item.scope = "global";

        std::string activeText = ToLowerUtf8(cellText(row, colActive));
        if (activeText == "нет" || activeText == "0" || activeText == "false" || activeText == "no") item.active = false;
        else item.active = true;

        if (ProfileToRuleCode(item.ruleCode.empty() ? item.profile : item.ruleCode).empty()) {
            ++csvBadRows;
            continue;
        }
        csvRows.push_back(std::move(item));
    }

    if (csvRows.empty() && csvBadRows == 0) csvError = "В файле нет строк данных";
    csvParsed = true;
}

void ManualTermsImportView::ImportCsvTerms() {
    if (!app || !app->HasDatabase()) return;
    if (!csvParsed || csvRows.empty()) {
        app->ShowError("Нет распознанных строк для импорта");
        return;
    }

    Database& db = app->Db();
    std::vector<Term> existing = db.ListTerms();

    auto findExisting = [&](const std::string& norm, int64_t ruleId, const std::string& listName) -> Term* {
        for (Term& t : existing) {
            if (t.termNorm == norm && t.ruleId == ruleId && EqualsUtf8Insensitive(t.listName, listName)) return &t;
        }
        return nullptr;
    };

    for (const CsvTermRow& row : csvRows) {
        std::string profileOrRule = row.ruleCode.empty() ? row.profile : row.ruleCode;
        std::string ruleCode = ProfileToRuleCode(profileOrRule);
        if (ruleCode.empty()) ruleCode = profileOrRule;
        int64_t ruleId = 0;
        for (const Rule& r : rules) {
            if (EqualsUtf8Insensitive(r.code, ruleCode)) { ruleId = r.id; break; }
        }
        if (ruleId == 0) {
            ++csvSkipped;
            continue;
        }

        std::string raw = CollapseSpacesUtf8(TrimUtf8(row.term));
        std::string norm = NormalizeTextUtf8(raw);
        if (norm.empty()) { ++csvSkipped; continue; }

        std::string listName = TrimUtf8(row.listName);
        if (listName.empty()) {
            listName = "Ручной перечень: " + (row.profile.empty() ? ruleCode : row.profile);
        }
        db.EnsureTermList(ruleId, listName);

        std::string sourceDoc = TrimUtf8(row.sourceDoc);
        if (sourceDoc.empty()) sourceDoc = "Ручной CSV-справочник ЛС";

        Term* old = findExisting(norm, ruleId, listName);
        if (old) {
            if (old->origin == "manual") {
                ++csvConflicts;
                continue;
            }
            old->term = raw;
            old->termNorm = norm;
            old->scope = row.scope.empty() ? "global" : ToLowerUtf8(row.scope);
            old->listName = listName;
            old->sourceDoc = sourceDoc;
            old->asOfDate = row.asOfDate;
            old->active = row.active;
            if (db.UpdateTerm(*old)) {
                ++csvUpdated;
            } else {
                ++csvSkipped;
            }
            continue;
        }

        Term term;
        term.ruleId = ruleId;
        term.ruleCode = ruleCode;
        term.term = raw;
        term.termNorm = norm;
        term.scope = row.scope.empty() ? "global" : ToLowerUtf8(row.scope);
        term.origin = "import";
        term.listName = listName;
        term.sourceDoc = sourceDoc;
        term.asOfDate = row.asOfDate;
        term.active = row.active;
        term.confidence = 1;
        if (db.AddTerm(term)) {
            ++csvImported;
            existing.push_back(term);
        } else {
            ++csvSkipped;
        }
    }

    app->MarkRulesChanged();
    app->RefreshAllViews();
    app->SetStatus("Импорт CSV: добавлено " + std::to_string(csvImported) +
                   ", обновлено " + std::to_string(csvUpdated) +
                   ", конфликтов " + std::to_string(csvConflicts) +
                   ", пропущено " + std::to_string(csvSkipped));
}

void ManualTermsImportView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("Сначала создайте или откройте базу (Файл → Создать/Открыть).");
            EndWindow();
            return;
        }

        if (rules.empty()) RefreshData();

        ImGui::Text(ICON_FA_FILE_CSV "  Импорт ручного перечня ЛС из CSV");
        ImGui::TextDisabled("Колонки: term/наименование, profile/профиль (ВИЧ, туберкулёз, вакцины, наркология, "
                            "тест-системы) или rule_code. Дополнительно: list_name, source_doc, as_of_date, "
                            "scope, active.");
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_FILE_IMPORT "  Выбрать CSV-файл")) {
            std::string start = app->DbPath().empty() ? "." :
                                std::filesystem::path(app->DbPath()).parent_path().string();
            IGFD::FileDialogConfig config;
            config.path = start;
            app->OpenFileDialog("ManualTermsCsv", "Импорт справочного CSV",
                                "CSV{.csv,.txt}", config);
        }
        app->ClaimDialog("ManualTermsCsv");

        if (ImGuiFileDialog::Instance()->Display("ManualTermsCsv", ImGuiWindowFlags_NoCollapse,
                                                ImVec2(600, 400), ImVec2(900, 700))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                csvPath = ImGuiFileDialog::Instance()->GetFilePathName();
                csvParsed = false;
                csvParseAttempted = false;
                csvError.clear();
                csvRows.clear();
                csvBadRows = 0;
                csvImported = csvUpdated = csvConflicts = csvSkipped = 0;
            }
            ImGuiFileDialog::Instance()->Close();
        }

        if (csvPath.empty()) {
            ImGui::TextDisabled("Файл не выбран.");
            EndWindow();
            return;
        }

        ImGui::Text(ICON_FA_FILE "  %s", std::filesystem::path(csvPath).filename().c_str());

        if (!csvParsed && !csvParseAttempted) {
            ParseCsvTerms();
        }

        if (!csvError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", csvError.c_str());
            if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Повторить чтение")) {
                csvParsed = false;
                csvParseAttempted = false;
            }
            EndWindow();
            return;
        }

        if (csvParsed) {
            ImGui::Text("Распознано строк: %zu | без правила/профиля: %d", csvRows.size(), csvBadRows);
            if (!csvRows.empty()) {
                std::unordered_map<std::string, int> byRule;
                for (const auto& r : csvRows) {
                    byRule[ProfileToRuleCode(r.ruleCode.empty() ? r.profile : r.ruleCode)]++;
                }
                for (const auto& kv : byRule) {
                    ImGui::TextDisabled("  %s: %d", kv.first.c_str(), kv.second);
                }
                if (ImGui::Button(ICON_FA_FILE_IMPORT "  Импортировать в термины")) {
                    ImportCsvTerms();
                }
            }
            if (csvImported || csvUpdated || csvConflicts || csvSkipped) {
                ImGui::TextDisabled("Результат: добавлено %d, обновлено %d, конфликтов %d, пропущено %d",
                                    csvImported, csvUpdated, csvConflicts, csvSkipped);
            }
        }

        EndWindow();
    }
}
