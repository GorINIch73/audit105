#include "ListsEditorView.h"
#include "TermsEditorView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) lines.push_back(line);
            line.clear();
        } else if (c == ',') {
            if (!line.empty()) lines.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

bool IsValidDate(const std::string& date) {
    if (date.size() != 10) return false;
    for (size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) {
            if (date[i] != '-') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
            return false;
        }
    }
    int month = std::atoi(date.substr(5, 2).c_str());
    int day = std::atoi(date.substr(8, 2).c_str());
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

}  // namespace

void ListsEditorView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    lists = app->Db().ListTermLists();
    rules = app->Db().ListRules();
    terms = app->Db().ListTerms();
    RebuildFiltered();
    initialized = true;
}

std::vector<Term> ListsEditorView::DraftTerms() const {
    std::vector<Term> out;
    for (const Term& term : terms) {
        if (term.ruleId == draft.ruleId && EqualsUtf8Insensitive(term.listName, draft.name)) {
            out.push_back(term);
        }
    }
    return out;
}

void ListsEditorView::RebuildFiltered() {
    filtered.clear();

    for (const TermList& list : lists) {
        std::vector<std::string> fields = {
            list.name, list.description, list.ruleCode, list.sourceDoc, list.asOfDate
        };
        if (!MatchesFilter(search, fields)) continue;
        filtered.push_back(list);
    }

    if (filtered.empty()) {
        selectedId = 0;
        page = 0;
        return;
    }

    bool found = false;
    for (const TermList& list : filtered) {
        if (list.id == selectedId) {
            found = true;
            break;
        }
    }
    if (!found) selectedId = filtered.front().id;

    int idx = SelectedIndex();
    if (idx >= 0) page = idx / pageSize;
    if (page < 0) page = 0;
    int totalPages = static_cast<int>((filtered.size() + static_cast<size_t>(pageSize) - 1) / static_cast<size_t>(pageSize));
    if (page >= totalPages) page = totalPages - 1;
}

int ListsEditorView::SelectedIndex() const {
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i].id == selectedId) return static_cast<int>(i);
    }
    return -1;
}

void ListsEditorView::Select(int64_t id) {
    selectedId = id;
    int idx = SelectedIndex();
    if (idx >= 0) page = idx / pageSize;
    LoadDraft();
}

void ListsEditorView::Navigate(int delta) {
    if (filtered.empty()) return;
    int idx = SelectedIndex();
    if (idx < 0) idx = 0;
    idx += delta;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(filtered.size())) idx = static_cast<int>(filtered.size()) - 1;
    Select(filtered[static_cast<size_t>(idx)].id);
}

void ListsEditorView::LoadDraft() {
    int idx = SelectedIndex();
    if (idx >= 0) {
        draft = filtered[static_cast<size_t>(idx)];
    } else {
        draft = TermList();
        draft.ruleId = 0;
        draft.active = true;
    }
}

void ListsEditorView::SaveDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранного перечня — нажмите «Добавить»");
        return;
    }
    draft.name = TrimUtf8(draft.name);
    if (draft.name.empty()) {
        app->ShowError("Название перечня обязательно");
        return;
    }
    if (!draft.asOfDate.empty() && !IsValidDate(draft.asOfDate)) {
        app->ShowError("Дата актуальности должна быть в формате ГГГГ-ММ-ДД");
        return;
    }
    for (const TermList& other : lists) {
        if (other.id != selectedId && other.ruleId == draft.ruleId &&
            EqualsUtf8Insensitive(other.name, draft.name)) {
            app->ShowError("Перечень «" + draft.name + "» уже есть у правила " +
                           (other.ruleCode.empty() ? "без правила" : other.ruleCode));
            return;
        }
    }

    draft.id = selectedId;
    if (app->Db().UpdateTermList(draft)) {
        app->MarkRulesChanged();
        app->SetStatus("Перечень обновлён: " + draft.name);
        RefreshData();
        Select(selectedId);
    } else {
        app->ShowError("Не удалось обновить перечень");
    }
}

void ListsEditorView::AddDraft() {
    if (!app || !app->HasDatabase()) return;
    int64_t id = app->Db().CreateBlankList(0);
    if (id == 0) {
        app->ShowError("Не удалось создать перечень");
        return;
    }
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Создан новый перечень (ID " + std::to_string(id) + ") — заполните поля и сохраните");
    RefreshData();
    Select(id);
}

void ListsEditorView::DuplicateDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранного перечня — выберите перечень, который нужно скопировать");
        return;
    }
    std::string sourceName = draft.name;
    int copied = 0;
    int64_t id = app->Db().DuplicateTermListById(selectedId, copied);
    if (id == 0) {
        app->ShowError("Не удалось скопировать перечень");
        return;
    }
    TermList copy;
    app->Db().GetTermList(id, copy);
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Скопирован перечень «" + sourceName + "» → «" + copy.name + "» (ID " +
                   std::to_string(id) + ", терминов: " + std::to_string(copied) + ")");
    RefreshData();
    Select(id);
}

void ListsEditorView::DeleteSelected() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        if (ImGui::Button(ICON_FA_TRASH "  Удалить")) {
            app->ShowError("Нет выбранного перечня — выберите его в таблице");
        }
        return;
    }
    std::string modalId = "delete_list_" + std::to_string(selectedId);
    if (ImGui::Button(ICON_FA_TRASH "  Удалить")) ImGui::OpenPopup(modalId.c_str());
    if (ConfirmationModal(modalId, "Удалить перечень",
                          "Удалить перечень «" + draft.name + "» вместе с его терминами (" +
                          std::to_string(draft.termCount) + ")? Правило останется. "
                          "Отменить это нельзя.",
                          "Удалить", "Отмена")) {
        if (app->Db().DeleteTermList(selectedId)) {
            app->MarkRulesChanged();
            app->SetStatus("Перечень удалён (терминов: " + std::to_string(draft.termCount) + ")");
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить перечень");
        }
    }
}

void ListsEditorView::AddTermsToDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Сначала выберите или создайте перечень");
        return;
    }
    if (draft.ruleId == 0) {
        app->ShowError("Перечню нужно назначить правило — термины добавляются только в перечень правила");
        return;
    }
    if (!bulkDate.empty() && !IsValidDate(bulkDate)) {
        app->ShowError("Дата актуальности должна быть в формате ГГГГ-ММ-ДД");
        return;
    }
    std::vector<std::string> lines = SplitLines(bulkText);
    if (lines.empty()) {
        app->ShowError("Введите термины — по одному на строку или через запятую");
        return;
    }
    int added = app->Db().AddTermsToList(draft.ruleId, draft.name, lines, bulkSource, bulkDate);
    app->MarkRulesChanged();
    app->SetStatus("Добавлено в «" + draft.name + "»: " + std::to_string(added) +
                   " (пропущено дубликатов: " + std::to_string(static_cast<int>(lines.size()) - added) + ")");
    bulkText.clear();
    RefreshData();
    Select(selectedId);
}

void ListsEditorView::DeleteFilteredLists() {
    if (!app || !app->HasDatabase()) return;
    if (filtered.empty()) {
        app->ShowError("По текущему фильтру нет перечней — измените фильтр");
        return;
    }

    int listTerms = 0;
    for (const TermList& list : filtered) {
        listTerms += list.termCount;
    }

    std::string modalId = "bulk_delete_lists_" + std::to_string(filtered.size());
    if (ImGui::Button((ICON_FA_TRASH "  Удалить перечни по фильтру (" +
                       std::to_string(filtered.size()) + ", терминов: " +
                       std::to_string(listTerms) + ")").c_str())) {
        ImGui::OpenPopup(modalId.c_str());
    }
    if (ConfirmationModal(modalId, "Удалить перечни по фильтру",
                         "Перечни будут очищены целиком (термины удалены). Правила останутся. "
                         "Отменить это нельзя.",
                         "Удалить", "Отмена")) {
        std::vector<TermList> targets;
        for (const TermList& list : filtered) targets.push_back(list);
        int removed = app->Db().DeleteTermLists(targets);
        if (removed > 0) {
            app->MarkRulesChanged();
            app->SetStatus("Очищено перечней: " + std::to_string(static_cast<int>(targets.size())) +
                           ", удалено терминов: " + std::to_string(removed));
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить перечни");
        }
    }
}

void ListsEditorView::RenderBulkOperations() {
    if (!app || !app->HasDatabase()) return;

    if (!ImGui::CollapsingHeader(ICON_FA_USERS "  Групповые операции")) return;

    ImGui::TextDisabled("Операции применяются ко всем перечням, прошедшим фильтр. Удаление необратимо.");
    ImGui::TextDisabled("Правила и термины здесь не удаляются — термины правятся в форме «Термины и списки».");
    DeleteFilteredLists();
}

void ListsEditorView::RenderFilters() {
    ImGui::Text(ICON_FA_FILTER "  Фильтр");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextDisabled("Фразы через запятую — И; внутри фразы поиск по всем полям — ИЛИ");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.24f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.95f, 1.0f, 1.0f));
    ImGui::InputText("##lists_filter", &search, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ERASER "  Очистить")) {
        search.clear();
    }
    ImGui::Separator();
    RebuildFiltered();
}

void ListsEditorView::RenderToolbar() {
    if (selectedId == 0) {
        ImGui::TextDisabled("Перечень не выбран");
    } else {
        ImGui::Text(ICON_FA_CIRCLE_INFO "  Выбор: ID %lld — «%s»",
                    static_cast<long long>(selectedId), draft.name.c_str());
    }

    if (ImGui::Button(ICON_FA_PLUS "  Добавить")) AddDraft();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_COPY "  Добавить копированием")) DuplicateDraft();
    ImGui::SameLine();
    DeleteSelected();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_LEFT "  Пред.")) Navigate(-1);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_RIGHT "  След.")) Navigate(1);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE "  Отменить правки")) LoadDraft();
    ImGui::Separator();
}

void ListsEditorView::RenderTable() {
    if (filtered.empty()) {
        ImGui::TextDisabled("Нет перечней по текущим фильтрам.");
        return;
    }

    int totalPages = static_cast<int>((filtered.size() + static_cast<size_t>(pageSize) - 1) / static_cast<size_t>(pageSize));
    if (page < 0) page = 0;
    if (page >= totalPages) page = totalPages - 1;

    int start = page * pageSize;
    int end = std::min(start + pageSize, static_cast<int>(filtered.size()));

    ImGui::Text("Показано %d–%d из %zu", start + 1, end, filtered.size());
    if (totalPages > 1) {
        if (page > 0 && ImGui::Button(ICON_FA_ARROW_LEFT "  стр.")) {
            --page;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("стр. %d/%d", page + 1, totalPages);
        ImGui::SameLine();
        if (page + 1 < totalPages && ImGui::Button(ICON_FA_ARROW_RIGHT "  стр.")) {
            ++page;
        }
    }
    ImGui::Separator();

    if (ImGui::BeginTable("lists_table", 8,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Перечень", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Описание", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Источник", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Терминов", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Актив", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (int i = start; i < end; ++i) {
            const TermList& list = filtered[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(list.id));

            bool isNew = list.id == highlightId;
            if (isNew) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(list.id).c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(list.name.empty() ? " " : list.name.c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(list.ruleCode.empty() ? " " : list.ruleCode.c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(list.description.empty() ? " " : list.description.c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(list.sourceDoc.empty() ? " " : list.sourceDoc.c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(list.asOfDate.empty() ? " " : list.asOfDate.c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(list.termCount).c_str(), selectedId == list.id)) {
                Select(list.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(list.active ? "да" : "нет", selectedId == list.id)) {
                Select(list.id);
            }

            if (isNew) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void ListsEditorView::RenderForm() {
    ImGui::Text(ICON_FA_LIST "  Редактор перечня");
    if (selectedId == 0) {
        ImGui::TextDisabled("Перечень не выбран — нажмите «Добавить» над таблицей");
        return;
    }
    ImGui::Text("ID: %lld", static_cast<long long>(selectedId));
    ImGui::Separator();

    std::vector<int64_t> ruleIds;
    std::vector<std::string> ruleLabels;
    for (const Rule& rule : rules) {
        ruleIds.push_back(rule.id);
        ruleLabels.push_back(rule.code);
    }
    int ruleIndex = -1;
    for (size_t i = 0; i < ruleIds.size(); ++i) {
        if (ruleIds[i] == draft.ruleId) {
            ruleIndex = static_cast<int>(i);
            break;
        }
    }
    std::string ruleText;

    if (ImGui::BeginTable("lists_form_fields", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        InputTextSync("Название", draft.name, FitItemWidth("Название"));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Правило"));
        ComboWithFilter("Правило", ruleLabels, ruleIndex, ruleText);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        InputTextSync("Источник", draft.sourceDoc, FitItemWidth("Источник"));
        ImGui::TableNextColumn();
        InputTextSync("Дата актуальности", draft.asOfDate, FitItemWidth("Дата актуальности"));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Порядок"));
        ImGui::InputInt("Порядок", &draft.sortOrder, 0, 1);
        ImGui::TableNextColumn();
        ImGui::Checkbox("Активен", &draft.active);

        ImGui::EndTable();
    }

    if (ruleIndex >= 0 && ruleIndex < static_cast<int>(ruleIds.size())) {
        draft.ruleId = ruleIds[static_cast<size_t>(ruleIndex)];
    }

    InputTextSync("Описание", draft.description, FitItemWidth("Описание"));

    ImGui::Separator();
    std::vector<Term> listTerms = DraftTerms();
    ImGui::Text(ICON_FA_BOOK_OPEN "  Термины перечня: %zu (активных: %zu)",
                listTerms.size(),
                static_cast<size_t>(std::count_if(listTerms.begin(), listTerms.end(),
                                                 [](const Term& term) { return term.active; })));

    if (listTerms.empty()) {
        ImGui::TextDisabled("В перечне пока нет терминов — добавьте их ниже или в форме «Термины».");
    } else {
        const int shown = std::min(static_cast<int>(listTerms.size()), 40);
        for (int i = 0; i < shown; ++i) {
            ImGui::TextDisabled("%s  [%s]", listTerms[static_cast<size_t>(i)].term.c_str(),
                                listTerms[static_cast<size_t>(i)].active ? "активен" : "выключен");
        }
        if (static_cast<int>(listTerms.size()) > shown) {
            ImGui::TextDisabled("… и ещё %zu терминов", listTerms.size() - static_cast<size_t>(shown));
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_LIGHTBULB "  Пакетное добавление: один термин на строку (или через запятую)");
    InputTextSync("Источник", bulkSource, FitItemWidth("Источник"));
    ImGui::SameLine();
    InputTextSync("Дата", bulkDate, 120.0f);
    ImGui::InputTextMultiline("##list_bulk_terms", &bulkText, ImVec2(0.0f, 120.0f));
    if (ImGui::Button(ICON_FA_DOWNLOAD "  Добавить термины в перечень")) AddTermsToDraft();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_BOOK_OPEN "  Открыть форму «Термины»")) {
        app->CreateView<TermsEditorView>("Термины");
        app->SetStatus("Форма «Термины» открыта — там можно править отдельные термины");
    }

    ImGui::Separator();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Сохранить")) SaveDraft();
}

void ListsEditorView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }

        if (!initialized) RefreshData();

        ImGui::Text(ICON_FA_LIST "  Перечни: %zu всего, %zu по фильтру", lists.size(), filtered.size());
        ImGui::TextDisabled("Перечень — это справочник терминов конкретного правила: название, источник, дата, активность.");
        ImGui::Separator();

        RenderFilters();
        RenderBulkOperations();
        ImGui::Separator();

        const float available = ImGui::GetContentRegionAvail().x;
        float height = ImGui::GetContentRegionAvail().y;
        if (height < 320.0f) height = 320.0f;
        const float minLeft = 300.0f;
        const float maxLeft = std::max(minLeft, available - 260.0f);
        listWidth = std::clamp(listWidth, minLeft, maxLeft);

        ImGui::BeginChild("lists_list", ImVec2(listWidth, height), true);
        RenderToolbar();
        RenderTable();
        ImGui::EndChild();

        ImGui::SameLine();
        VerticalSplitter("lists_split", &listWidth, minLeft, maxLeft, 10.0f, height);
        ImGui::SameLine();

        ImGui::BeginChild("lists_form", ImVec2(available - listWidth - 14.0f, height), true);
        RenderForm();
        ImGui::EndChild();

        EndWindow();
    }
}
