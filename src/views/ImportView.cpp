#define IMGUI_DEFINE_MATH_OPERATORS

#include "ImportView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../ImGuiFileDialog.h"
#include "../Widgets.h"
#include "../domain/FormatDetector.h"
#include "../domain/OdsReader.h"
#include "../domain/XlsxReader.h"
#include "../domain/XmlReader.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace {

std::string ShellQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + '\'';
}

std::string GuessPeriod(const std::string& name) {
    for (size_t i = 0; i + 4 <= name.size(); ++i) {
        bool digits = true;
        for (size_t j = 0; j < 4; ++j) {
            if (!std::isdigit(static_cast<unsigned char>(name[i + j]))) {
                digits = false;
                break;
            }
        }
        if (digits && name[i] == '2') return name.substr(i, 4);
    }
    return "";
}

bool ConvertWithSoffice(const std::string& path, std::string& outPath) {
    std::string outDir = "/tmp/audit105_convert";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::string cmd = "soffice --headless --convert-to ods --outdir " + ShellQuote(outDir) +
                      " " + ShellQuote(path);
    if (std::system(cmd.c_str()) != 0) return false;
    outPath = outDir + "/" + std::filesystem::path(path).stem().string() + ".ods";
    return std::filesystem::exists(outPath);
}

bool ReadSpreadsheet(const std::string& path, InputFormat format, Spreadsheet& sheet,
                     std::string& error) {
    switch (format) {
        case InputFormat::Xlsx: return ReadXlsx(path, sheet, error);
        case InputFormat::Xml: return ReadXml(path, sheet, error);
        default: return ReadOds(path, sheet, error);
    }
}

}  // namespace

void ImportView::RefreshData() {
    parsed = false;
}

void ImportView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("Сначала создайте или откройте базу (Файл → Создать/Открыть).");
            EndWindow();
            return;
        }

        if (ImGui::Button(ICON_FA_FILE_IMPORT "  Выбрать файл оборота")) {
            std::string start = app->DbPath().empty()
                                    ? "."
                                    : std::filesystem::path(app->DbPath()).parent_path().string();
            IGFD::FileDialogConfig config;
            config.path = start;
            app->OpenFileDialog("ImportFile", "Импорт оборота",
                                "Оборот{.ods,.xlsx,.xls,.xml}", config);
        }
        app->ClaimDialog("ImportFile");

        if (ImGuiFileDialog::Instance()->Display("ImportFile", ImGuiWindowFlags_NoCollapse,
                                                ImVec2(600, 400), ImVec2(900, 700))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                path = ImGuiFileDialog::Instance()->GetFilePathName();
                parsed = false;
                parseAttempted = false;
                lastError.clear();
                app->LogEvent("Выбран файл оборота: " +
                              std::filesystem::path(path).filename().string());
            }
            ImGuiFileDialog::Instance()->Close();
        }

        if (path.empty()) {
            ImGui::TextDisabled("Файл не выбран. Поддерживаются .ods, .xlsx, .xls (через LibreOffice), .xml.");
            EndWindow();
            return;
        }

        std::string effectivePath = path;
        InputFormat format = DetectFormat(path);

        if (!parsed && !parseAttempted) {
            parseAttempted = true;
            app->SetProgress("Читаем файл оборота: " +
                             std::filesystem::path(path).filename().string(), -1.0);

            if (format == InputFormat::Unknown) {
                lastError = "Не удалось определить формат файла: " + path;
            } else if (NeedsConversion(format)) {
                std::string converted;
                if (!ConvertWithSoffice(path, converted)) {
                    lastError = "Не удалось конвертировать " + std::string(FormatName(format)) +
                                " в ODS. Нужен LibreOffice (soffice) или конвертируйте вручную.";
                } else {
                    effectivePath = converted;
                }
            }

            if (lastError.empty()) {
                Spreadsheet sheet;
                InputFormat readFormat = DetectFormat(effectivePath);
                if (!ReadSpreadsheet(effectivePath, readFormat, sheet, lastError)) {
                    parsed = false;
                } else {
                    report = ParsePositions(sheet);
                    sheetName = sheet.sheetName;
                    formatName = FormatName(readFormat);
                    parsed = true;
                    if (period.empty()) period = GuessPeriod(std::filesystem::path(path).filename().string());
                }
            }

            app->ClearProgress();
            app->LogEvent(parsed ? "Файл оборота прочитан: позиций " + std::to_string(report.items.size())
                                 : "Не удалось прочитать файл оборота: " + lastError,
                          parsed ? LogSuccess : LogError);
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_FILE "  %s", std::filesystem::path(path).filename().c_str());
        ImGui::TextDisabled("Формат: %s | лист: %s | строк: %d",
                            formatName.c_str(), sheetName.c_str(), report.sumRows + report.subtotalRows);

        if (!parsed) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
            if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Повторить чтение файла")) {
                parseAttempted = false;
                lastError.clear();
            }
            EndWindow();
            return;
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_LIST "  Профиль импорта");
        ImGui::Text("Позиций: %d | подитогов: %d | строк «Кол.»: %d",
                    static_cast<int>(report.items.size()), report.subtotalRows, report.qtyRows);
        ImGui::Text("Арифметика не сходится: %d | без «Кол.»: %d | точных дублей: %d | дублей по имени: %d",
                    report.arithFail, report.missingQty, report.duplicates, report.nameDuplicates);
        ImGui::Text("Без оборота: %d | отрицательных остатков: %d", report.noTurnover, report.negativeClosing);
        ImGui::Text("Остаток на конец: %s", FormatAmount(report.closingTotal).c_str());

        ImGui::Separator();
        if (ImGui::BeginTable("subs", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Субсчёт");
            ImGui::TableSetupColumn("Откр.");
            ImGui::TableSetupColumn("Дебет");
            ImGui::TableSetupColumn("Кредит");
            ImGui::TableSetupColumn("Закр.");
            ImGui::TableSetupColumn("Арифм.");
            ImGui::TableHeadersRow();
            int rowId = 1;
            for (const Subaccount& sub : report.subaccounts) {
                ImGui::TableNextRow();
                ImGui::PushID(rowId);
                ImGui::TableNextColumn();
                ImGui::Text("%d", rowId);
                ImGui::TableNextColumn();
                ImGui::Text("%s%s", sub.code.c_str(), sub.isTotal ? " (итого)" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(sub.opening).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(sub.debit).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(sub.credit).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAmount(sub.closing).c_str());
                ImGui::TableNextColumn();
                ImGui::Text(sub.arithOk ? "ok" : "сбой");
                ImGui::PopID();
                ++rowId;
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        std::string fileName = std::filesystem::path(path).filename().string();
        int existing = app->Db().CountRunsByFile(fileName);
        if (existing > 0) {
            ImGui::TextDisabled("В реестре уже %d прогонов этого файла — импорт создаст новую запись.", existing);
        }

        ImGui::Separator();
        InputTextSync("Организация", organization);
        InputTextSync("Период", period);
        InputTextSync("Примечание", note);
        InputTextSync("Счёт", account);
        InputTextSync("КФО", kfo);

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_UPLOAD "  Импортировать в базу")) {
            Run run;
            run.file = fileName;
            run.format = formatName;
            run.organization = organization;
            run.period = period;
            run.note = note;
            run.account = account;
            run.kfo = kfo;

            int64_t runId = app->Db().ImportRun(run, report);
            if (runId != 0) {
                app->MarkRunRulesApplied(runId);
                app->SetStatus("Импортировано позиций: " + std::to_string(report.items.size()));
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Импорт завершён.");
            } else {
                lastError = "Импорт не выполнен (проверьте базу и транзакцию).";
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
            }
        }

        EndWindow();
    }
}
