#define IMGUI_DEFINE_MATH_OPERATORS

#include "RegistryImportView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../ImGuiFileDialog.h"
#include "../Widgets.h"
#include "../domain/CsvReader.h"
#include "../domain/FormatDetector.h"
#include "../domain/OdsReader.h"
#include "../domain/Utf8.h"
#include "../domain/XmlReader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace {

std::string FileName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::string ParentDir(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

std::string GuessAsOfDate(const std::string& fileName) {
    for (size_t i = 0; i + 8 <= fileName.size(); ++i) {
        bool digits = true;
        for (size_t j = 0; j < 8; ++j) {
            if (!std::isdigit(static_cast<unsigned char>(fileName[i + j]))) {
                digits = false;
                break;
            }
        }
        if (!digits) continue;
        std::string year = fileName.substr(i, 4);
        std::string month = fileName.substr(i + 4, 2);
        std::string day = fileName.substr(i + 6, 2);
        if (year[0] == '2') return year + "-" + month + "-" + day;
    }
    return std::string();
}

bool ReadRegistryFile(const std::string& path, std::string& sheetName,
                      std::vector<XlsxSheet>& sheets, Spreadsheet& sheet, std::string& error) {
    InputFormat format = DetectFormat(path);
    switch (format) {
        case InputFormat::Xlsx: {
            if (!ListXlsxSheets(path, sheets, error)) return false;
            if (sheetName.empty()) {
                const XlsxSheet* best = &sheets[0];
                for (const XlsxSheet& s : sheets) {
                    if (s.size > best->size) best = &s;
                }
                sheetName = best->name;
            }
            return ReadXlsx(path, sheet, error, sheetName);
        }
        case InputFormat::Csv:
            sheets.clear();
            return ReadCsv(path, sheet, error);
        case InputFormat::Xml:
            sheets.clear();
            return ReadXml(path, sheet, error);
        case InputFormat::Ods:
            sheets.clear();
            return ReadOds(path, sheet, error);
        default:
            error = "Формат файла не поддерживается (ожидается .xlsx, .csv, .ods, .xml)";
            return false;
    }
}

std::unordered_map<std::string, RegistryNodeInfo> BuildNodesFromLink(const std::string& linkPath) {
    std::unordered_map<std::string, RegistryNodeInfo> nodes;
    if (linkPath.empty()) return nodes;

    Spreadsheet nodeSheet;
    std::vector<XlsxSheet> nodeSheets;
    std::string nodeSheetName;
    std::string nodeError;
    if (!ReadRegistryFile(linkPath, nodeSheetName, nodeSheets, nodeSheet, nodeError)) return nodes;

    RegistryProfile nodeProfile;
    if (!BuildRegistryProfile(nodeSheet, nodeProfile, nodeError)) return nodes;
    return BuildNodeIndex(nodeProfile);
}

// Один проход файла: результат всегда пишется в state, повторных попыток нет.
bool ParseRegistry(const std::string& path, std::string& sheetName, const std::string& linkPath,
                   const std::vector<ControlFilter>& filters, RegistryScanState& state,
                   std::string& error) {
    InputFormat format = DetectFormat(path);
    if (format == InputFormat::Xlsx) {
        if (!ListXlsxSheets(path, state.sheets, error)) return false;
        if (sheetName.empty()) {
            const XlsxSheet* best = &state.sheets[0];
            for (const XlsxSheet& s : state.sheets) {
                if (s.size > best->size) best = &s;
            }
            sheetName = best->name;
        }
        state.sheetName = sheetName;

        std::string fileName = ToLowerUtf8(FileName(path));
        bool isKlp = ContainsUtf8(ToLowerUtf8(sheetName), "klp") || ContainsUtf8(fileName, "klp");

        if (isKlp) {
            std::unordered_map<std::string, RegistryNodeInfo> nodes = BuildNodesFromLink(linkPath);
            if (!ScanRegistryFile(path, sheetName, filters, nodes, state.profile, error,
                                  &state.progress)) {
                return false;
            }
            state.linked = state.profile.linked;
            state.streamed = true;
        } else {
            Spreadsheet sheet;
            if (!ReadXlsx(path, sheet, error, sheetName)) return false;
            if (!BuildRegistryProfile(sheet, state.profile, error, &state.progress)) return false;
            if (state.profile.kind == RegistryFormat::Klp && !linkPath.empty()) {
                Spreadsheet linkSheet;
                std::vector<XlsxSheet> linkSheets;
                std::string linkSheetName;
                std::string linkError;
                if (ReadRegistryFile(linkPath, linkSheetName, linkSheets, linkSheet, linkError)) {
                    RegistryProfile linkProfile;
                    if (BuildRegistryProfile(linkSheet, linkProfile, linkError)) {
                        state.linked = LinkEntriesByNodeCode(linkProfile, state.profile);
                    } else {
                        error = "Не удалось прочитать файл СМНН для связи: " + linkError;
                    }
                } else {
                    error = "Не удалось прочитать файл СМНН для связи: " + linkError;
                }
            }
        }
    } else {
        state.sheets.clear();
        Spreadsheet sheet;
        if (!ReadRegistryFile(path, sheetName, state.sheets, sheet, error)) return false;
        state.sheetName = sheetName;
        if (!BuildRegistryProfile(sheet, state.profile, error, &state.progress)) return false;
    }

    if (state.profile.entries.empty()) {
        error = "В файле не найдено ни одного совпадения с фильтрами федерального контроля";
        return false;
    }
    return true;
}

}  // namespace

RegistryImportView::RegistryImportView() {
    SetTitle("Федеральные справочники");
    filters = DefaultControlFilters();
}

RegistryImportView::~RegistryImportView() {
    if (scan) scan->progress.cancel.store(true);
    if (scanThread.joinable()) scanThread.join();
    if (app) app->ClearProgress();
}

void RegistryImportView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    sources = app->Db().ListRegistrySources();
    conflicts = app->Db().ListConflicts(true);
}

void RegistryImportView::StartScan() {
    if (scanThread.joinable()) scanThread.join();
    scan.reset();

    auto state = std::make_shared<RegistryScanState>();
    state->sheetName = sheetName;

    std::string filePath = path;
    std::string link = linkPath;
    std::vector<ControlFilter> activeFilters = filters;

    scan = state;
    scanStart = std::chrono::steady_clock::now();
    parsed = false;
    lastError.clear();
    lastMessage.clear();

    app->SetProgress("Читаем справочник: " + FileName(filePath), -1.0);
    app->LogEvent("Начинаем чтение справочника: " + FileName(filePath));

    scanThread = std::thread([state, filePath, link, activeFilters]() {
        state->ok = ParseRegistry(filePath, state->sheetName, link, activeFilters, *state,
                                  state->error);
        state->done.store(true);
    });
}

void RegistryImportView::FinishScan() {
    if (!scan) return;
    if (scanThread.joinable()) scanThread.join();

    profile = std::move(scan->profile);
    sheets = std::move(scan->sheets);
    sheetName = scan->sheetName;
    linked = scan->linked;
    streamed = scan->streamed;
    parsed = scan->ok;
    lastError = scan->error;

    if (parsed) {
        if (asOfDate.empty()) {
            asOfDate = GuessAsOfDate(FileName(path));
            if (asOfDate.empty() && !profile.entries.empty()) asOfDate = profile.entries.front().asOfDate;
        }
        if (streamed) {
            matches = profile.matches;
        } else {
            RecomputeMatches();
        }
        app->LogEvent("Справочник прочитан: строк " + std::to_string(profile.dataRows) +
                      ", совпадений " + std::to_string(profile.entries.size()) +
                      ", пропущено " + std::to_string(profile.skipped), LogSuccess);
    } else {
        app->LogEvent("Не удалось прочитать справочник: " + lastError, LogError);
    }

    app->ClearProgress();
    scan.reset();
}

void RegistryImportView::RecomputeMatches() {
    matches.assign(filters.size(), {});
    for (size_t f = 0; f < filters.size(); ++f) {
        for (size_t i = 0; i < profile.entries.size(); ++i) {
            int level = MatchLevel(profile.entries[i], filters[f]);
            if (level > 0) {
                profile.entries[i].matchLevel = level;
                profile.entries[i].matchWhy = filters[f].code;
                matches[f].push_back(i);
            }
        }
    }
}

void RegistryImportView::StartImport() {
    importQueue.clear();
    for (size_t i = 0; i < filters.size(); ++i) {
        if (filters[i].enabled && !matches[i].empty()) importQueue.push_back(i);
    }

    if (importQueue.empty()) {
        lastError = "Нет включённых фильтров с совпадениями — импорт не начат.";
        app->LogEvent(lastError, LogWarn);
        return;
    }

    importIndex = 0;
    imported = 0;
    totalAdded = 0;
    totalConflicts = 0;
    importing = true;
    lastError.clear();
    lastMessage.clear();
    app->SetProgress("Импортируем перечни: 0 из " + std::to_string(importQueue.size()), 0.0);
}

void RegistryImportView::StepImport() {
    if (importIndex >= importQueue.size()) {
        importing = false;
        app->ClearProgress();
        lastMessage = "Импортировано перечней: " + std::to_string(imported) +
                      " | терминов добавлено: " + std::to_string(totalAdded) +
                      " | конфликтов: " + std::to_string(totalConflicts);
        app->LogEvent(lastMessage, LogSuccess);
        RefreshData();
        app->MarkRulesChanged();
        return;
    }

    size_t i = importQueue[importIndex];
    ControlFilter& filter = filters[i];

    std::vector<Rule> rules = app->Db().ListRules();
    int64_t ruleId = 0;
    for (const Rule& rule : rules) {
        if (EqualsUtf8Insensitive(rule.code, filter.ruleCode)) {
            ruleId = rule.id;
            break;
        }
    }

    if (ruleId == 0) {
        lastError = "Правило " + filter.ruleCode + " не найдено — создайте его в «Правилах».";
        app->LogEvent(lastError, LogWarn);
        ++importIndex;
        app->SetProgress("Импортировано перечней: " + std::to_string(imported) + " из " +
                         std::to_string(importQueue.size()),
                         static_cast<double>(importIndex) / static_cast<double>(importQueue.size()));
        return;
    }

    std::vector<RegistryEntry> selected;
    selected.reserve(matches[i].size());
    for (int idx : matches[i]) selected.push_back(profile.entries[static_cast<size_t>(idx)]);

    RegistrySource source;
    source.name = FileName(path);
    source.file = source.name;
    source.ruleCode = filter.ruleCode;
    source.listName = filter.title;
    source.format = profile.format;
    source.asOfDate = asOfDate;
    source.note = note;
    int64_t sourceId = app->Db().RegisterRegistrySource(source);

    RegistryImportStats stats;
    if (app->Db().ImportRegistryEntries(ruleId, filter.title, selected, sourceId, source.name,
                                        asOfDate, stats)) {
        ++imported;
        totalAdded += stats.added;
        totalConflicts += stats.conflicts;
        app->LogEvent("Перечень «" + filter.title + "»: добавлено " + std::to_string(stats.added) +
                      ", конфликтов " + std::to_string(stats.conflicts), LogSuccess);
    } else {
        app->LogEvent("Перечень «" + filter.title + "»: импорт не выполнен", LogWarn);
    }

    ++importIndex;
    app->SetProgress("Импортируем перечни: " + std::to_string(importIndex) + " из " +
                     std::to_string(importQueue.size()),
                     static_cast<double>(importIndex) / static_cast<double>(importQueue.size()));
}

void RegistryImportView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("Сначала создайте или откройте базу (Файл → Создать/Открыть).");
            EndWindow();
            return;
        }

        if (ImGui::Button(ICON_FA_FILE_IMPORT "  Выбрать файл справочника")) {
            std::string start = app->DbPath().empty() ? "." : ParentDir(app->DbPath());
            IGFD::FileDialogConfig config;
            config.path = start;
            app->OpenFileDialog("RegistryFile", "Федеральный справочник",
                                "Справочник{.xlsx,.csv,.ods,.xml}", config);
        }
        app->ClaimDialog("RegistryFile");

        if (ImGuiFileDialog::Instance()->Display("RegistryFile", ImGuiWindowFlags_NoCollapse,
                                                ImVec2(600, 400), ImVec2(900, 700))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                path = ImGuiFileDialog::Instance()->GetFilePathName();
                sheetName.clear();
                StartScan();
            }
            ImGuiFileDialog::Instance()->Close();
        }

        if (path.empty()) {
            ImGui::TextDisabled("Файл не выбран. Поддерживаются выгрузки ЕСКЛП/ГРЛС: .xlsx, .csv, .ods, .xml.");
            EndWindow();
            return;
        }

        if (scan && !scan->done) {
            int rows = scan->progress.rows.load();
            int found = scan->progress.matches.load();
            int total = scan->progress.totalRows.load();
            int seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::steady_clock::now() - scanStart).count());

            ImGui::Separator();
            ImGui::Text(ICON_FA_FILE "  %s", FileName(path).c_str());
            ImGui::Text(ICON_FA_HOURGLASS "  Идёт чтение справочника…");

            if (total > 0) {
                float fraction = static_cast<float>(rows) / static_cast<float>(total);
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 14.0f));
                ImGui::TextDisabled("Обработано %d из ~%d строк | совпадений: %d | прошло %d с",
                                    rows, total, found, seconds);
            } else {
                float fraction = static_cast<float>(seconds % 3) / 3.0f;
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 14.0f));
                ImGui::TextDisabled("Обработано %d строк | совпадений: %d | прошло %d с",
                                    rows, found, seconds);
            }

            ImGui::TextDisabled("Интерфейс отвечает — кнопки станут доступны после завершения чтения.");
            if (ImGui::Button(ICON_FA_CIRCLE_STOP "  Отменить чтение")) {
                scan->progress.cancel.store(true);
                app->SetStatus("Отменяем чтение справочника…");
            }
            EndWindow();
            return;
        }

        if (scan && scan->done) FinishScan();

        ImGui::Separator();
        ImGui::Text(ICON_FA_FILE "  %s", FileName(path).c_str());
        if (!parsed) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
            if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Повторить чтение файла")) StartScan();
            EndWindow();
            return;
        }

        ImGui::TextDisabled("Формат: %s | лист: %s | строк справочника: %d | пропущено: %d",
                            profile.format.c_str(), profile.sheetName.c_str(),
                            static_cast<int>(profile.entries.size()), profile.skipped);
        ImGui::TextDisabled("Схема: %s | заголовок: строка %d | данные: с строки %d",
                            profile.kindName.c_str(), profile.headerRow + 1, profile.dataStartRow + 1);
        if (!profile.warning.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", profile.warning.c_str());
        }
        if (streamed) {
            ImGui::TextDisabled("Построчный проход большого КЛП: строк %d, совпадений %d, "
                                "связано с узлами СМНН %d",
                                profile.dataRows, static_cast<int>(profile.entries.size()), linked);
        }
        if (profile.kind == RegistryFormat::Klp) {
            if (ImGui::Button(ICON_FA_LINK "  Связать позиции КЛП с узлами СМНН")) {
                IGFD::FileDialogConfig linkConfig;
                linkConfig.path = ParentDir(path);
                app->OpenFileDialog("RegistryLinkFile", "Файл СМНН для связи",
                                    "СМНН{.xlsx,.csv,.ods,.xml}", linkConfig);
            }
            app->ClaimDialog("RegistryLinkFile");
            if (ImGuiFileDialog::Instance()->Display("RegistryLinkFile", ImGuiWindowFlags_NoCollapse,
                                                    ImVec2(600, 400), ImVec2(900, 700))) {
                if (ImGuiFileDialog::Instance()->IsOk()) {
                    linkPath = ImGuiFileDialog::Instance()->GetFilePathName();
                    StartScan();
                }
                ImGuiFileDialog::Instance()->Close();
            }
            if (!linkPath.empty()) {
                ImGui::TextDisabled("Связка по коду узла СМНН: %s (%d позиций подтянуто АТХ/ФТГ/ОКПД2)",
                                    FileName(linkPath).c_str(), linked);
            }
        }

        if (!sheets.empty()) {
            std::vector<const char*> names;
            int selected = 0;
            for (size_t i = 0; i < sheets.size(); ++i) {
                names.push_back(sheets[i].name.c_str());
                if (sheets[i].name == sheetName) selected = static_cast<int>(i);
            }
            if (ImGui::Combo("Лист", &selected, names.data(), static_cast<int>(names.size()))) {
                sheetName = sheets[static_cast<size_t>(selected)].name;
                StartScan();
            }
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_TABLE "  Распознанные колонки");
        ImGui::TextDisabled("МНН: %s | торговое: %s | ФТГ: %s | АТХ: %s | ОКПД2: %s | дата: %s",
                            profile.map.nameCol >= 0 ? profile.headers[profile.map.nameCol].c_str() : "—",
                            profile.map.altNameCol >= 0 ? profile.headers[profile.map.altNameCol].c_str() : "—",
                            profile.map.groupCol >= 0 ? profile.headers[profile.map.groupCol].c_str() : "—",
                            profile.map.atxCol >= 0 ? profile.headers[profile.map.atxCol].c_str() : "—",
                            profile.map.okpdCol >= 0 ? profile.headers[profile.map.okpdCol].c_str() : "—",
                            profile.map.dateCol >= 0 ? profile.headers[profile.map.dateCol].c_str() : "—");

        ImGui::Separator();
        ImGui::Text(ICON_FA_FILTER "  Фильтры федерального контроля");
        InputTextSync("Дата справочника", asOfDate);
        InputTextSync("Примечание источника", note);

        bool filtersChanged = false;

        if (ImGui::BeginTable("filters", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Правило");
            ImGui::TableSetupColumn("Фильтр");
            ImGui::TableSetupColumn("Критерии");
            ImGui::TableSetupColumn("Совпадений");
            ImGui::TableSetupColumn("Перечень");
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < filters.size(); ++i) {
                ControlFilter& filter = filters[i];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                ImGui::TableNextColumn();
                if (InputTextSync("##rule", filter.ruleCode, 70.0f)) filtersChanged = true;

                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##on", &filter.enabled)) filtersChanged = true;
                ImGui::SameLine();
                ImGui::TextWrapped("%s", filter.title.c_str());

                ImGui::TableNextColumn();
                std::string criteria;
                for (const std::string& prefix : filter.atxPrefixes) {
                    if (!criteria.empty()) criteria += ", ";
                    criteria += "АТХ " + prefix;
                }
                for (const std::string& prefix : filter.okpdPrefixes) {
                    if (!criteria.empty()) criteria += ", ";
                    criteria += "ОКПД2 " + prefix;
                }
                for (const std::string& mask : filter.ftgMasks) {
                    if (!criteria.empty()) criteria += ", ";
                    criteria += "ФТГ *" + mask + "*";
                }
                for (const std::string& mask : filter.nameMasks) {
                    if (!criteria.empty()) criteria += ", ";
                    criteria += "*" + mask + "*";
                }
                if (filter.masksForDevicesOnly) criteria += " (маски — только для медизделий: без АТХ и ФТГ)";
                for (const std::string& mask : filter.excludeMasks) {
                    criteria += " | не " + mask;
                }
                ImGui::TextWrapped("%s", criteria.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%d", static_cast<int>(matches[i].size()));

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", filter.title.c_str());

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (filtersChanged) {
            if (streamed) {
                ImGui::TextDisabled("Фильтры изменены — нажмите «Пересчитать совпадения» (нужен повторный проход файла).");
                if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Пересчитать совпадения")) StartScan();
            } else {
                RecomputeMatches();
            }
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_EYE "  Предпросмотр совпадений");
        ImGui::SliderInt("Строк в предпросмотре", &previewLimit, 5, 100);

        for (size_t i = 0; i < filters.size(); ++i) {
            if (!filters[i].enabled || matches[i].empty()) continue;
            if (ImGui::BeginTable(("preview" + std::to_string(i)).c_str(), 5,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Наименование");
                ImGui::TableSetupColumn("АТХ");
                ImGui::TableSetupColumn("ФТГ / группа");
                ImGui::TableSetupColumn("Дата");
                ImGui::TableSetupColumn("Основание");
                ImGui::TableHeadersRow();
                size_t shown = std::min(matches[i].size(), static_cast<size_t>(previewLimit));
                for (size_t j = 0; j < shown; ++j) {
                    const RegistryEntry& entry = profile.entries[static_cast<size_t>(matches[i][j])];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", entry.name.c_str());
                    ImGui::TableNextColumn();
                    std::string atx;
                    for (const std::string& code : entry.atx) {
                        if (!atx.empty()) atx += "; ";
                        atx += code;
                    }
                    ImGui::TextDisabled("%s", atx.empty() ? "—" : atx.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", entry.group.empty() ? "—" : entry.group.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", entry.asOfDate.empty() ? "—" : entry.asOfDate.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", ConfidenceLabel(entry.matchLevel).c_str());
                }
                if (matches[i].size() > shown) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("… ещё %zu", matches[i].size() - shown);
                }
                ImGui::EndTable();
            }
        }

        ImGui::Separator();
        if (importing) {
            ImGui::Text(ICON_FA_SPINNER "  Импорт выполняется…");
            ImGui::ProgressBar(static_cast<float>(importIndex) / static_cast<float>(importQueue.size()),
                              ImVec2(-1.0f, 14.0f));
            ImGui::TextDisabled("Перечней: %d из %d | терминов добавлено: %d | конфликтов: %d",
                                static_cast<int>(importIndex), static_cast<int>(importQueue.size()),
                                totalAdded, totalConflicts);
            StepImport();
        } else if (ImGui::Button(ICON_FA_UPLOAD "  Импорт совпадений в справочник")) {
            StartImport();
        }

        if (!lastMessage.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "%s", lastMessage.c_str());
        }
        if (!lastError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_DATABASE "  Источники справочников");
        if (sources.empty()) {
            ImGui::TextDisabled("Импортированных источников пока нет.");
        } else if (ImGui::BeginTable("sources", 6,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Источник");
            ImGui::TableSetupColumn("Правило");
            ImGui::TableSetupColumn("Перечень");
            ImGui::TableSetupColumn("Дата");
            ImGui::TableSetupColumn("Строк / терминов");
            ImGui::TableSetupColumn("Действия");
            ImGui::TableHeadersRow();
            for (RegistrySource& source : sources) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(source.id));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", source.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", source.ruleCode.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", source.listName.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", source.asOfDate.empty() ? "—" : source.asOfDate.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d / %d (конфликтов %d)", source.rows, source.added, source.conflicts);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##active", &source.active);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH "  удалить")) {
                    int removed = 0;
                    app->Db().DeleteRegistrySource(source.id, removed);
                    RefreshData();
                    app->SetStatus("Источник удалён, терминов удалено: " + std::to_string(removed));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_EXCLAMATION "  Конфликтов с ручными терминами: %d",
                    app->Db().CountUnresolvedConflicts());
        if (!conflicts.empty() &&
            ImGui::BeginTable("conflicts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Термин");
            ImGui::TableSetupColumn("Правило / перечень");
            ImGui::TableSetupColumn("Источник");
            ImGui::TableSetupColumn("Решение");
            ImGui::TableHeadersRow();
            for (const ConflictRow& conflict : conflicts) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(conflict.id));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", conflict.manualTerm.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s / %s", conflict.ruleCode.c_str(), conflict.listName.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", conflict.sourceDoc.c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Оставить ручной")) {
                    app->Db().ResolveConflict(conflict.id, "manual");
                    RefreshData();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Заменить")) {
                    app->Db().ResolveConflict(conflict.id, "import");
                    RefreshData();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        EndWindow();
    }
}
