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

const char* OriginFilterNames[] = {"Все", "manual", "import"};
const char* ScopeFilterNames[] = {"Все", "local", "global"};
const std::vector<std::string> kScopeValues = {"local", "global"};
const std::vector<std::string> kOriginValues = {"manual", "import"};

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

const char* ListGetter(void* data, int idx) {
    return (*reinterpret_cast<std::vector<std::string>*>(data))[static_cast<size_t>(idx)].c_str();
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

void TermsEditorView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    terms = app->Db().ListTerms();
    rules = app->Db().ListRules();
    lists = app->Db().ListTermLists();
    PruneSelection();
    RebuildFiltered();
    RebuildFilteredLists();
    initialized = true;
}

void TermsEditorView::PruneSelection() {
    std::vector<int64_t> keptTerms;
    for (int64_t id : selectedTermIds) {
        for (const Term& term : terms) {
            if (term.id == id) {
                keptTerms.push_back(id);
                break;
            }
        }
    }
    selectedTermIds = keptTerms;

    std::vector<int64_t> keptRuleIds;
    std::vector<std::string> keptNames;
    for (size_t i = 0; i < selectedListRuleIds.size(); ++i) {
        for (const TermList& list : lists) {
            if (list.ruleId == selectedListRuleIds[i] &&
                EqualsUtf8Insensitive(list.name, selectedListNames[i])) {
                keptRuleIds.push_back(list.ruleId);
                keptNames.push_back(list.name);
                break;
            }
        }
    }
    selectedListRuleIds = keptRuleIds;
    selectedListNames = keptNames;
}

bool TermsEditorView::IsTermSelected(int64_t id) const {
    return std::find(selectedTermIds.begin(), selectedTermIds.end(), id) != selectedTermIds.end();
}

bool TermsEditorView::IsListSelected(int64_t ruleId, const std::string& listName) const {
    for (size_t i = 0; i < selectedListRuleIds.size(); ++i) {
        if (selectedListRuleIds[i] == ruleId && EqualsUtf8Insensitive(selectedListNames[i], listName)) {
            return true;
        }
    }
    return false;
}

void TermsEditorView::ToggleTerm(int64_t id) {
    if (IsTermSelected(id)) {
        selectedTermIds.erase(std::remove(selectedTermIds.begin(), selectedTermIds.end(), id),
                             selectedTermIds.end());
    } else {
        selectedTermIds.push_back(id);
    }
}

void TermsEditorView::ToggleList(int64_t ruleId, const std::string& listName) {
    if (IsListSelected(ruleId, listName)) {
        for (size_t i = 0; i < selectedListRuleIds.size(); ++i) {
            if (selectedListRuleIds[i] == ruleId && EqualsUtf8Insensitive(selectedListNames[i], listName)) {
                selectedListRuleIds.erase(selectedListRuleIds.begin() + static_cast<long>(i));
                selectedListNames.erase(selectedListNames.begin() + static_cast<long>(i));
                return;
            }
        }
    } else {
        selectedListRuleIds.push_back(ruleId);
        selectedListNames.push_back(listName);
    }
}

std::vector<int64_t> TermsEditorView::SelectedRuleIds() const {
    std::vector<int64_t> out;
    for (size_t i = 0; i < selectedListRuleIds.size(); ++i) {
        if (selectedListRuleIds[i] != 0) out.push_back(selectedListRuleIds[i]);
    }
    for (int64_t id : selectedTermIds) {
        for (const Term& term : terms) {
            if (term.id == id && term.ruleId != 0) {
                out.push_back(term.ruleId);
                break;
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void TermsEditorView::RebuildFiltered() {
    filtered.clear();
    const std::string needle = ToLowerUtf8(search);

    for (const Term& term : terms) {
        if (activeOnly && !term.active) continue;
        if (ruleFilterId != 0 && term.ruleId != ruleFilterId) continue;
        if (!listFilter.empty() && !EqualsUtf8Insensitive(term.listName, listFilter)) continue;
        if (!originFilter.empty() && !EqualsUtf8Insensitive(term.origin, originFilter)) continue;
        if (!scopeFilter.empty() && !EqualsUtf8Insensitive(term.scope, scopeFilter)) continue;

        if (!needle.empty()) {
            bool hit = ContainsUtf8(term.term, needle) ||
                       ContainsUtf8(term.termNorm, needle) ||
                       ContainsUtf8(term.ruleCode, needle) ||
                       ContainsUtf8(term.listName, needle) ||
                       ContainsUtf8(term.sourceDoc, needle) ||
                       ContainsUtf8(term.asOfDate, needle);
            if (!hit) continue;
        }

        filtered.push_back(term);
    }

    if (filtered.empty()) {
        selectedId = 0;
        page = 0;
        return;
    }

    bool found = false;
    for (const Term& term : filtered) {
        if (term.id == selectedId) {
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

void TermsEditorView::RebuildFilteredLists() {
    filteredLists.clear();
    const std::string needle = ToLowerUtf8(search);

    for (const TermList& list : lists) {
        if (ruleFilterId != 0 && list.ruleId != ruleFilterId) continue;
        if (!needle.empty() &&
            !ContainsUtf8(list.name, needle) &&
            !ContainsUtf8(list.ruleCode, needle) &&
            !ContainsUtf8(list.description, needle)) {
            continue;
        }
        filteredLists.push_back(list);
    }
}

int TermsEditorView::SelectedIndex() const {
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i].id == selectedId) return static_cast<int>(i);
    }
    return -1;
}

void TermsEditorView::Select(int64_t id) {
    selectedId = id;
    int idx = SelectedIndex();
    if (idx >= 0) page = idx / pageSize;
    LoadDraft();
}

void TermsEditorView::Navigate(int delta) {
    if (filtered.empty()) return;
    int idx = SelectedIndex();
    if (idx < 0) idx = 0;
    idx += delta;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(filtered.size())) idx = static_cast<int>(filtered.size()) - 1;
    Select(filtered[static_cast<size_t>(idx)].id);
}

void TermsEditorView::LoadDraft() {
    int idx = SelectedIndex();
    if (idx >= 0) {
        draft = filtered[static_cast<size_t>(idx)];
    } else {
        draft = Term();
        draft.scope = "local";
        draft.origin = "manual";
        draft.active = true;
    }
}

void TermsEditorView::SaveDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранной записи — нажмите «Добавить»");
        return;
    }
    if (draft.term.empty()) {
        app->ShowError("Термин обязателен");
        return;
    }
    draft.scope = ToLowerUtf8(TrimUtf8(draft.scope));
    draft.origin = ToLowerUtf8(TrimUtf8(draft.origin));
    draft.listName = TrimUtf8(draft.listName);
    if (draft.scope.empty()) draft.scope = "local";
    if (draft.origin.empty()) draft.origin = "manual";
    if (!IsEnumValue(draft.scope, kScopeValues)) {
        app->ShowError("Scope должен быть «local» или «global»");
        return;
    }
    if (!IsEnumValue(draft.origin, kOriginValues)) {
        app->ShowError("Origin должен быть «manual» или «import»");
        return;
    }
    if (!draft.asOfDate.empty() && !IsValidDate(draft.asOfDate)) {
        app->ShowError("Дата актуальности должна быть в формате ГГГГ-ММ-ДД");
        return;
    }
    draft.id = selectedId;
    if (app->Db().UpdateTerm(draft)) {
        app->MarkRulesChanged();
        app->SetStatus("Термин обновлён: " + draft.term);
        RefreshData();
        Select(selectedId);
    } else {
        app->ShowError("Не удалось обновить термин");
    }
}

void TermsEditorView::AddDraft() {
    if (!app || !app->HasDatabase()) return;
    int64_t id = app->Db().CreateBlankTerm(ruleFilterId, listFilter);
    if (id == 0) {
        app->ShowError("Не удалось создать термин");
        return;
    }
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Создан новый термин (ID " + std::to_string(id) + ") — заполните поля и сохраните");
    RefreshData();
    Select(id);
}

void TermsEditorView::DuplicateDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранной записи — выберите термин, который нужно скопировать");
        return;
    }
    std::string sourceTerm = draft.term;
    int64_t id = app->Db().DuplicateTerm(selectedId);
    if (id == 0) {
        app->ShowError("Не удалось скопировать термин");
        return;
    }
    Term copy;
    app->Db().GetTerm(id, copy);
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Скопирован термин «" + sourceTerm + "» → «" + copy.term + "» (ID " +
                   std::to_string(id) + ") — поправьте поля и сохраните");
    RefreshData();
    Select(id);
}

void TermsEditorView::SelectList(int64_t ruleId, const std::string& listName) {
    listFilter = listName;
    selectedListRuleId = ruleId;
    RebuildFiltered();
}

void TermsEditorView::DuplicateSelectedList() {
    if (!app || !app->HasDatabase()) return;

    int64_t ruleId = selectedListRuleId != 0 ? selectedListRuleId : ruleFilterId;
    bool listExists = false;
    for (const TermList& list : lists) {
        if (EqualsUtf8Insensitive(list.name, listFilter) && list.ruleId == ruleId) {
            listExists = true;
            break;
        }
    }
    if (!listExists) {
        app->ShowError("Нет выбранного списка — выберите перечень в таблице «Списки терминов»");
        return;
    }

    std::string sourceName = listFilter;
    int copied = 0;
    std::string newName = app->Db().DuplicateTermList(ruleId, listFilter, copied);
    if (newName.empty()) {
        app->ShowError("Не удалось скопировать список — в нём нет терминов");
        return;
    }
    listFilter = newName;
    selectedListRuleId = ruleId;
    app->MarkRulesChanged();
    app->SetStatus("Скопирован список «" + sourceName + "» → «" + newName + "» (" +
                   std::to_string(copied) + " терминов)");
    RefreshData();
    RebuildFiltered();
}

void TermsEditorView::DeleteSelected() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        if (ImGui::Button(ICON_FA_TRASH "  Удалить")) {
            app->ShowError("Нет выбранной записи — выберите термин в таблице");
        }
        return;
    }
    std::string modalId = "delete_term_" + std::to_string(selectedId);
    if (ImGui::Button(ICON_FA_TRASH "  Удалить")) ImGui::OpenPopup(modalId.c_str());
    if (ConfirmationModal(modalId, "Удалить термин",
                         "Удалить термин «" + draft.term + "»? Отменить это нельзя.",
                         "Удалить", "Отмена")) {
        if (app->Db().DeleteTerm(selectedId)) {
            app->MarkRulesChanged();
            app->SetStatus("Термин удалён");
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить термин");
        }
    }
}

void TermsEditorView::RenderLists() {
    std::string listsLabel = ICON_FA_LIST "  Списки терминов (" + std::to_string(filteredLists.size()) +
                             " из " + std::to_string(lists.size()) + ")";
    if (!ImGui::CollapsingHeader(listsLabel.c_str())) {
        return;
    }

    if (filteredLists.empty()) {
        ImGui::TextDisabled("Списков нет — термины не привязаны к перечням (проверьте фильтр «Правило»).");
    } else {
        if (ImGui::BeginTable("term_lists", 6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("Выбор", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Перечень", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Источник", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Терминов", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Активных", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < filteredLists.size(); ++i) {
                const TermList& list = filteredLists[i];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                ImGui::TableNextColumn();
                bool checked = IsListSelected(list.ruleId, list.name);
                if (ImGui::Checkbox("##list_pick", &checked)) {
                    ToggleList(list.ruleId, list.name);
                }

                ImGui::TableNextColumn();
                ImGui::Text("%s", list.ruleCode.empty() ? "—" : list.ruleCode.c_str());

                ImGui::TableNextColumn();
                std::string label = list.name.empty() ? "(без названия)" : list.name;
                if (ImGui::Selectable(label.c_str(), EqualsUtf8Insensitive(list.name, listFilter))) {
                    SelectList(list.ruleId, list.name);
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", list.sourceDoc.empty() ? "—" : list.sourceDoc.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%d", list.termCount);

                ImGui::TableNextColumn();
                ImGui::Text("%d", list.activeTermCount);

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_LIGHTBULB "  Пакетное добавление: один термин на строку (или через запятую) — в выбранный перечень");
    InputTextSync("Перечень", listFilter, 200.0f);
    ImGui::SameLine();
    InputTextSync("Источник", bulkSource, 200.0f);
    ImGui::SameLine();
    InputTextSync("Дата", bulkDate, 120.0f);

    ImGui::InputTextMultiline("##bulk_terms", &bulkText, ImVec2(0.0f, 120.0f));
    if (ImGui::Button(ICON_FA_DOWNLOAD "  Добавить термины в список")) {
        if (ruleFilterId == 0) {
            app->ShowError("Выберите правило — термины добавляются в список конкретного правила");
        } else if (!bulkDate.empty() && !IsValidDate(bulkDate)) {
            app->ShowError("Дата актуальности должна быть в формате ГГГГ-ММ-ДД");
        } else {
            std::vector<std::string> lines = SplitLines(bulkText);
            int added = app->Db().AddTermsToList(ruleFilterId, listFilter, lines, bulkSource, bulkDate);
            if (added > 0) app->MarkRulesChanged();
            app->SetStatus("Добавлено терминов: " + std::to_string(added) + " (пропущено дубликатов: " +
                           std::to_string(static_cast<int>(lines.size()) - added) + ")");
            bulkText.clear();
            RefreshData();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_COPY "  Скопировать список")) DuplicateSelectedList();
}

void TermsEditorView::ClearFilters() {
    search.clear();
    ruleFilterId = 0;
    listFilter.clear();
    originFilter.clear();
    scopeFilter.clear();
    activeOnly = false;
    RebuildFiltered();
    RebuildFilteredLists();
}

void TermsEditorView::SelectAllFiltered() {
    selectedTermIds.clear();
    for (const Term& term : filtered) selectedTermIds.push_back(term.id);
    selectedListRuleIds.clear();
    selectedListNames.clear();
    for (const TermList& list : filteredLists) {
        selectedListRuleIds.push_back(list.ruleId);
        selectedListNames.push_back(list.name);
    }
}

void TermsEditorView::DeleteSelectedRules() {
    if (!app || !app->HasDatabase()) return;

    std::vector<int64_t> ruleIds = SelectedRuleIds();
    if (ruleIds.empty()) {
        app->ShowError("Нет отмеченных правил — отметьте перечни или термины");
        return;
    }

    int ruleTerms = 0;
    int ruleFlags = 0;
    for (int64_t ruleId : ruleIds) {
        ruleTerms += app->Db().CountRuleTerms(ruleId);
        ruleFlags += app->Db().CountRuleFlags(ruleId);
    }

    std::string modalId = "bulk_delete_rules_" + std::to_string(ruleIds.size());
    ImGui::TextDisabled("Отмечено правил: %zu (терминов: %d, отметок: %d)",
                        ruleIds.size(), ruleTerms, ruleFlags);
    if (ImGui::Button(ICON_FA_TRASH "  Удалить правила по отмеченным")) {
        ImGui::OpenPopup(modalId.c_str());
    }
    if (ConfirmationModal(modalId, "Удалить правила",
                          "Удалить " + std::to_string(ruleIds.size()) + " правил, их термины (" +
                          std::to_string(ruleTerms) + ") и отметки позиций (" +
                          std::to_string(ruleFlags) + ")? После этого можно импортировать новые данные. "
                          "Отменить это нельзя.",
                          "Удалить", "Отмена")) {
        int removedTerms = 0;
        int removedFlags = 0;
        int removed = app->Db().DeleteRules(ruleIds, removedTerms, removedFlags);
        if (removed > 0) {
            app->MarkRulesChanged();
            app->SetStatus("Удалено правил: " + std::to_string(removed) +
                           ", терминов: " + std::to_string(removedTerms) +
                           ", отметок: " + std::to_string(removedFlags) +
                           " — можно импортировать новый справочник");
            selectedTermIds.clear();
            selectedListRuleIds.clear();
            selectedListNames.clear();
            selectedListRuleId = 0;
            listFilter.clear();
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить правила");
        }
    }
}

void TermsEditorView::RenderBulkOperations() {
    if (!app || !app->HasDatabase()) return;

    int selectedListTerms = 0;
    for (size_t i = 0; i < selectedListRuleIds.size(); ++i) {
        for (const TermList& list : lists) {
            if (list.ruleId == selectedListRuleIds[i] &&
                EqualsUtf8Insensitive(list.name, selectedListNames[i])) {
                selectedListTerms += list.termCount;
                break;
            }
        }
    }

    ImGui::Text(ICON_FA_USERS "  Групповые операции");
    ImGui::TextDisabled("Отметьте списки или термины — операции применяются только к отмеченным");

    if (ImGui::Button(ICON_FA_CHECK_DOUBLE "  Отметить все по фильтру")) {
        SelectAllFiltered();
        app->SetStatus("Отмечено терминов: " + std::to_string(selectedTermIds.size()) +
                       ", перечней: " + std::to_string(selectedListRuleIds.size()));
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "  Снять отметки")) {
        selectedTermIds.clear();
        selectedListRuleIds.clear();
        selectedListNames.clear();
    }

    if (selectedTermIds.empty() && selectedListRuleIds.empty()) {
        ImGui::TextDisabled("Ничего не отмечено.");
        return;
    }

    if (!selectedTermIds.empty()) {
        std::string deleteTermsLabel = ICON_FA_TRASH "  Удалить отмеченные термины (" +
                                       std::to_string(selectedTermIds.size()) + ")";
        if (ImGui::Button(deleteTermsLabel.c_str())) {
            ImGui::OpenPopup("bulk_delete_terms");
        }
        if (ImGui::BeginPopup("bulk_delete_terms")) {
            ImGui::TextDisabled("Будет удалено терминов: %zu. Отменить это нельзя.",
                                selectedTermIds.size());
            if (ImGui::Button(ICON_FA_TRASH "  Подтвердить удаление")) {
                int removed = app->Db().DeleteTerms(selectedTermIds);
                app->MarkRulesChanged();
                app->SetStatus("Удалено терминов: " + std::to_string(removed));
                selectedTermIds.clear();
                selectedId = 0;
                highlightId = 0;
                ImGui::CloseCurrentPopup();
                RefreshData();
            }
            ImGui::EndPopup();
        }
    }

    if (!selectedListRuleIds.empty()) {
        std::string deleteListsLabel = ICON_FA_TRASH "  Удалить отмеченные перечни (" +
                                       std::to_string(selectedListRuleIds.size()) + ", терминов: " +
                                       std::to_string(selectedListTerms) + ")";
        if (ImGui::Button(deleteListsLabel.c_str())) {
            ImGui::OpenPopup("bulk_delete_lists");
        }
        if (ImGui::BeginPopup("bulk_delete_lists")) {
            ImGui::TextDisabled("Перечни будут очищены целиком (термины удалены). "
                                "Правила останутся, пока у них есть термины.");
            std::vector<TermList> targets;
            for (size_t i = 0; i < selectedListRuleIds.size(); ++i) {
                TermList list;
                list.ruleId = selectedListRuleIds[i];
                list.name = selectedListNames[i];
                targets.push_back(list);
            }
            if (ImGui::Button(ICON_FA_TRASH "  Подтвердить удаление")) {
                int removed = app->Db().DeleteTermLists(targets);
                app->MarkRulesChanged();
                app->SetStatus("Очищено перечней: " + std::to_string(static_cast<int>(targets.size())) +
                               ", удалено терминов: " + std::to_string(removed));
                selectedListRuleIds.clear();
                selectedListNames.clear();
                listFilter.clear();
                ImGui::CloseCurrentPopup();
                RefreshData();
            }
            ImGui::EndPopup();
        }
    }

    DeleteSelectedRules();

    std::vector<int64_t> ruleIds = SelectedRuleIds();
    int emptyRules = 0;
    for (int64_t ruleId : ruleIds) {
        if (app->Db().CountRuleTerms(ruleId) == 0) ++emptyRules;
    }
    if (emptyRules > 0) {
        std::string deleteRulesLabel = ICON_FA_GAVEL "  Удалить правила без терминов (" +
                                       std::to_string(emptyRules) + ")";
        if (ImGui::Button(deleteRulesLabel.c_str())) {
            ImGui::OpenPopup("bulk_delete_rules");
        }
        if (ImGui::BeginPopup("bulk_delete_rules")) {
            ImGui::TextDisabled("У правил отмеченных перечней больше нет терминов — их можно "
                                "удалить, чтобы импортировать новые данные.");
            if (ImGui::Button(ICON_FA_TRASH "  Подтвердить удаление")) {
                int removed = app->Db().DeleteRulesWithoutTerms(ruleIds);
                app->MarkRulesChanged();
                app->SetStatus("Удалено правил: " + std::to_string(removed) +
                               " — можно импортировать новый справочник");
                selectedListRuleIds.clear();
                selectedListNames.clear();
                ImGui::CloseCurrentPopup();
                RefreshData();
            }
            ImGui::EndPopup();
        }
    }
}

void TermsEditorView::RenderFilters() {
    std::vector<int64_t> ruleIds;
    std::vector<std::string> ruleLabels;
    for (const Rule& rule : rules) {
        ruleIds.push_back(rule.id);
        ruleLabels.push_back(rule.code);
    }

    int ruleIndex = -1;
    int originIndex = 0;
    int scopeIndex = 0;
    std::string ruleText;
    for (size_t i = 0; i < ruleIds.size(); ++i) {
        if (ruleIds[i] == ruleFilterId) {
            ruleIndex = static_cast<int>(i);
            break;
        }
    }

    ImGui::Text(ICON_FA_FILTER "  Фильтры");
    if (ImGui::BeginTable("terms_filters", 3, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Поиск", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Параметры", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Действия", ImGuiTableColumnFlags_WidthFixed, 190.0f);

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        InputTextSync("Поиск", search, FitItemWidth("Поиск"));

        ImGui::TableNextColumn();
        ImGui::Checkbox("Только активные", &activeOnly);

        ImGui::TableNextColumn();
        if (ImGui::Button(ICON_FA_ERASER "  Очистить все фильтры")) {
            search.clear();
            activeOnly = false;
            ruleIndex = -1;
            originIndex = 0;
            scopeIndex = 0;
            listFilter.clear();
        }

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Правило"));
        ComboWithFilter("Правило", ruleLabels, ruleIndex, ruleText);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Источник"));
        ImGui::Combo("Источник", &originIndex, OriginFilterNames, 3);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Scope"));
        ImGui::Combo("Scope", &scopeIndex, ScopeFilterNames, 3);

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        InputTextSync("Перечень", listFilter, FitItemWidth("Перечень"));

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Перечни редактируются в форме «Перечни»");

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Выбрано терминов: %zu, списков: %zu",
                            selectedTermIds.size(), selectedListRuleIds.size());

        ImGui::EndTable();
    }

    ruleFilterId = (ruleIndex >= 0 && ruleIndex < static_cast<int>(ruleIds.size()))
                   ? ruleIds[static_cast<size_t>(ruleIndex)]
                   : 0;
    originFilter = originIndex == 0 ? std::string() : OriginFilterNames[originIndex];
    scopeFilter = scopeIndex == 0 ? std::string() : ScopeFilterNames[scopeIndex];

    RebuildFiltered();
    RebuildFilteredLists();
}

void TermsEditorView::RenderToolbar() {
    if (selectedId == 0) {
        ImGui::TextDisabled("Запись не выбрана");
    } else {
        ImGui::Text(ICON_FA_CIRCLE_INFO "  Выбор: ID %lld — «%s»",
                    static_cast<long long>(selectedId), draft.term.c_str());
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

void TermsEditorView::RenderTable() {
    if (filtered.empty()) {
        ImGui::TextDisabled("Нет терминов по текущим фильтрам.");
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

    if (ImGui::BeginTable("terms_table", 8,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Выбор", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Термин", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Перечень", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Источник", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Правило", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Актив", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (int i = start; i < end; ++i) {
            const Term& term = filtered[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(term.id));

            bool isNew = term.id == highlightId;
            if (isNew) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
            }

            ImGui::TableNextColumn();
            bool checked = IsTermSelected(term.id);
            if (ImGui::Checkbox("##term_pick", &checked)) {
                ToggleTerm(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(term.id).c_str(), selectedId == term.id)) {
                Select(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(term.term.empty() ? " " : term.term.c_str(), selectedId == term.id)) {
                Select(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(term.listName.empty() ? " " : term.listName.c_str(), selectedId == term.id)) {
                Select(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(term.sourceDoc.empty() ? " " : term.sourceDoc.c_str(), selectedId == term.id)) {
                Select(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(term.ruleCode.empty() ? " " : term.ruleCode.c_str(), selectedId == term.id)) {
                Select(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(term.asOfDate.empty() ? " " : term.asOfDate.c_str(), selectedId == term.id)) {
                Select(term.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(term.active ? "да" : "нет", selectedId == term.id)) {
                Select(term.id);
            }

            if (isNew) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void TermsEditorView::RenderForm() {
    ImGui::Text(ICON_FA_BOOK_OPEN "  Редактор термина");
    if (selectedId == 0) {
        ImGui::TextDisabled("Запись не выбрана — нажмите «Добавить» над таблицей");
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

    std::vector<std::string> listNames;
    std::vector<int64_t> listRuleIds;
    for (const TermList& list : lists) {
        if (list.name.empty()) continue;
        listNames.push_back(list.name);
        listRuleIds.push_back(list.ruleId);
    }
    if (!draft.listName.empty() && !IsEnumValue(draft.listName, listNames)) {
        listNames.push_back(draft.listName);
        listRuleIds.push_back(draft.ruleId);
    }

    if (ImGui::BeginTable("terms_form_fields", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        InputTextSync("Термин", draft.term, FitItemWidth("Термин"));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Правило"));
        ComboWithFilter("Правило", ruleLabels, ruleIndex, ruleText);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ComboEnum("Scope", kScopeValues, draft.scope);
        ImGui::TableNextColumn();
        ComboEnum("Origin", kOriginValues, draft.origin);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (listNames.empty()) {
            ImGui::TextDisabled("Перечень (у этого правила списков нет)");
        } else {
            ComboEnum("Перечень", listNames, draft.listName);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS "  новый перечень")) ImGui::OpenPopup("term_list_new");
        if (ImGui::BeginPopup("term_list_new")) {
            std::string newName = draft.listName;
            InputTextSync("Название перечня", newName, 220.0f);
            ImGui::TextDisabled("Перечень — это справочник терминов правила; его описание, источник и дату "
                                "можно задать в форме «Перечни».");
            if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Применить")) {
                draft.listName = TrimUtf8(newName);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        InputTextSync("Дата", draft.asOfDate, FitItemWidth("Дата"));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Правило перечня: %s", draft.ruleId == 0 ? "не указано" : draft.ruleCode.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Перечни: %zu у этого правила",
                            static_cast<size_t>(std::count(listRuleIds.begin(), listRuleIds.end(), draft.ruleId)));

        ImGui::EndTable();
    }

    if (ruleIndex >= 0 && ruleIndex < static_cast<int>(ruleIds.size())) {
        draft.ruleId = ruleIds[static_cast<size_t>(ruleIndex)];
    }

    InputTextSync("Источник", draft.sourceDoc, FitItemWidth("Источник"));
    ImGui::Checkbox("Активен", &draft.active);

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_LIGHTBULB "  Scope — где действует термин (local — только в этом перечне, global — во всех). Origin — как получен: вручную или импортом. Дата — ГГГГ-ММ-ДД.");
    ImGui::TextDisabled("Норма для поиска (регистр и пробелы нормализуются автоматически): «%s»",
                        draft.termNorm.c_str());
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Сохранить")) SaveDraft();
}

void TermsEditorView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }

        if (!initialized) RefreshData();

        ImGui::Text(ICON_FA_BOOK_OPEN "  Термины и списки: %zu всего, %zu по фильтру", terms.size(), filtered.size());
        ImGui::TextDisabled("Списки недопустимых лекарств и другие справочники редактируются построчно.");
        ImGui::Separator();

        RenderLists();
        RenderFilters();
        RenderBulkOperations();
        ImGui::Separator();

        const float available = ImGui::GetContentRegionAvail().x;
        float height = ImGui::GetContentRegionAvail().y;
        if (height < 320.0f) height = 320.0f;
        const float minLeft = 300.0f;
        const float maxLeft = std::max(minLeft, available - 260.0f);
        listWidth = std::clamp(listWidth, minLeft, maxLeft);

        ImGui::BeginChild("terms_list", ImVec2(listWidth, height), true);
        RenderToolbar();
        RenderTable();
        ImGui::EndChild();

        ImGui::SameLine();
        VerticalSplitter("terms_split", &listWidth, minLeft, maxLeft, 10.0f, height);
        ImGui::SameLine();

        ImGui::BeginChild("terms_form", ImVec2(available - listWidth - 14.0f, height), true);
        RenderForm();
        ImGui::EndChild();

        EndWindow();
    }
}
