#include "RulesEditorView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace {

const char* RuleTypeFilterNames[] = {"Все", "structural", "exact", "synonym", "term", "regex"};
const char* RuleTypeNames[] = {"structural", "exact", "synonym", "term", "regex"};
const char* SeverityFilterNames[] = {"Все", "0", "1", "2", "3"};
const char* SeverityNames[] = {"0 — низкая", "1 — средняя", "2 — высокая", "3 — критичная"};

int TypeIndex(const std::string& type) {
    for (int i = 0; i < 5; ++i) {
        if (type == RuleTypeNames[i]) return i;
    }
    return 0;
}

int64_t CategoryFilterId(const std::vector<int64_t>& ids, int index) {
    if (index >= 0 && index < static_cast<int>(ids.size())) {
        return ids[static_cast<size_t>(index)];
    }
    return 0;
}


std::vector<std::string> SplitCommaList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : text) {
        if (ch == ',' || ch == ';' || ch == '\n') {
            std::string part = TrimUtf8(current);
            if (!part.empty()) parts.push_back(part);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    std::string part = TrimUtf8(current);
    if (!part.empty()) parts.push_back(part);
    return parts;
}

bool LooksLikeSubaccount(const std::string& code) {
    bool hasDigit = false;
    for (char ch : code) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            hasDigit = true;
        } else if (ch != '.') {
            return false;
        }
    }
    return hasDigit;
}

std::string JoinSubaccounts(const std::vector<std::string>& codes) {
    std::string result;
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) result += ", ";
        result += codes[i];
    }
    return result;
}

void RenderPatternHelp(const std::string& type, const PatternParts& parts, std::string& body) {
    ImGui::Separator();
    ImGui::Text(ICON_FA_LIGHTBULB "  Пояснение к шаблону");
    ImGui::TextWrapped(
        "Шаблон состоит из двух частей: «sub:субсчёты; тело». Часть «sub:» ограничивает, какие субсчёты "
        "проверяются (без неё — все). Тело задаёт саму проверку и зависит от типа правила. "
        "Наименования позиций сравниваются в нормализованном виде: нижний регистр, без лишних пробелов.");
    ImGui::TextDisabled("Нажмите на ключевое слово в таблице — оно подставится в тело шаблона.");

    if (ImGui::BeginTable("pattern_keywords", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Ключевое слово", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Логика", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Пример", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableHeadersRow();
        for (const KeywordHelp& info : KeywordHelpFor(type)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (info.keyword.empty()) {
                ImGui::TextUnformatted(info.name.c_str());
            } else {
                if (ImGui::Selectable(info.name.c_str(), false,
                                      ImGuiSelectableFlags_NoPadWithHalfSpacing)) {
                    body = info.keyword;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Нажмите, чтобы подставить «%s» в тело шаблона",
                                      info.keyword.c_str());
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", info.logic.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", info.example.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Text(ICON_FA_CODE_COMPARE "  Разбор шаблона: %s",
                parts.subaccounts.empty() ? "все субсчёты" : JoinSubaccounts(parts.subaccounts).c_str());
    ImGui::Indent();
    ImGui::Text("тело: «%s»", body.empty() ? "(пусто)" : body.c_str());

    bool ok = true;
    for (const std::string& code : parts.subaccounts) {
        if (!LooksLikeSubaccount(code)) {
            ImGui::TextDisabled("%s  «%s» — не похож на субсчёт (ожидается вид 105.31)",
                                ICON_FA_CIRCLE_EXCLAMATION, code.c_str());
            ok = false;
        }
    }

    if (type == "structural") {
        std::string keyword = KeywordName(body);
        if (keyword.empty()) {
            ImGui::TextDisabled("%s  Тело пустое — правило не сработает. Выберите проверку из списка выше.",
                                ICON_FA_CIRCLE_EXCLAMATION);
            ok = false;
        } else if (!IsEnumValue(keyword, StructuralKeywords())) {
            ImGui::TextDisabled("%s  «%s» — неизвестная проверка для типа structural",
                                ICON_FA_CIRCLE_EXCLAMATION, keyword.c_str());
            ok = false;
        }
    } else if (type == "exact") {
        std::string keyword = KeywordName(body);
        if (keyword.empty() || !IsEnumValue(keyword, ExactKeywords())) {
            ImGui::TextDisabled("%s  Для exact нужно signature или name", ICON_FA_CIRCLE_EXCLAMATION);
            ok = false;
        }
    } else if (type == "synonym" && body.empty()) {
        ImGui::TextDisabled("%s  Список синонимов пуст — правило не сработает", ICON_FA_CIRCLE_EXCLAMATION);
        ok = false;
    } else if (type == "regex") {
        if (body.empty()) {
            ImGui::TextDisabled("%s  Регулярное выражение пустое", ICON_FA_CIRCLE_EXCLAMATION);
            ok = false;
        } else {
            try {
                std::regex expression(body, std::regex::ECMAScript);
                (void)expression;
            } catch (const std::regex_error& error) {
                ImGui::TextDisabled("%s  Выражение не компилируется: %s",
                                    ICON_FA_CIRCLE_EXCLAMATION, error.what());
                ok = false;
            }
        }
    }

    if (ok) ImGui::TextDisabled("Шаблон корректен для типа «%s».", type.c_str());
    ImGui::Unindent();
}

void RenderLogicHelp(const std::vector<Rule>& rules) {
    if (!ImGui::CollapsingHeader(ICON_FA_LIGHTBULB "  Логика правил (примеры при создании базы)")) {
        return;
    }

    ImGui::TextWrapped(
        "Правила применяются по типам: structural → exact → synonym → term → regex. "
        "Одна позиция может получить несколько отметок; в форме и отчётах используется отметка с наибольшей серьёзностью.");

    struct Group {
        const char* category;
        const char* logic;
    };
    const Group groups[] = {
        {"Недопустимо по ОМС",
         "term-правила: правило задаёт субсчёт и серьёзность, а сам список ЛС (вакцины, иммуноглобулины, АРТ при ВИЧ, "
         "противотуберкулезные, наркология, тест-системы ВИЧ/гепатитов) хранится в «Термины и списки» и редактируется построчно. "
         "Позиция отмечается, если её название содержит хотя бы один термин списка."},
        {"Неверный субсчёт",
         "synonym-правила: немедицинские товары и медизделия на субсчёте лекарственных препаратов (105.31) — признак неверного субсчёта."},
        {"Кандидаты в ОС",
         "synonym + regex + порог оборота: оборудование, мебель, инвентарь и техника на субсчётах МЗ должны закупаться "
         "как основные средства (ОС), а не по счёту 105. Отдельное structural-правило с порогом ловит «замороженные» остатки."},
        {"Обратить внимание",
         "structural и exact: не сходится арифметика, нет строки «Кол.», отрицательный остаток, дубликаты позиции."},
    };

    for (const Group& group : groups) {
        int count = 0;
        for (const Rule& rule : rules) {
            if (EqualsUtf8Insensitive(rule.category, group.category)) ++count;
        }
        ImGui::Text("%s — %d правил", group.category, count);
        ImGui::Indent();
        ImGui::TextWrapped("%s", group.logic);
        for (const Rule& rule : rules) {
            if (EqualsUtf8Insensitive(rule.category, group.category)) {
                ImGui::TextDisabled("%s [%s] %s", rule.code.c_str(), rule.type.c_str(),
                                    rule.pattern.empty() ? "(список в терминах)" : rule.pattern.c_str());
            }
        }
        ImGui::Unindent();
    }
}

}  // namespace

void RulesEditorView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    rules = app->Db().ListRules();
    categories = app->Db().ListCategories();
    terms = app->Db().ListTerms();
    PruneSelection();
    RebuildFiltered();
    initialized = true;
}

bool RulesEditorView::IsRuleSelected(int64_t id) const {
    return std::find(selectedRuleIds.begin(), selectedRuleIds.end(), id) != selectedRuleIds.end();
}

void RulesEditorView::ToggleRule(int64_t id) {
    if (IsRuleSelected(id)) {
        selectedRuleIds.erase(std::remove(selectedRuleIds.begin(), selectedRuleIds.end(), id),
                             selectedRuleIds.end());
    } else {
        selectedRuleIds.push_back(id);
    }
}

void RulesEditorView::PruneSelection() {
    std::vector<int64_t> kept;
    for (int64_t id : selectedRuleIds) {
        for (const Rule& rule : rules) {
            if (rule.id == id) {
                kept.push_back(id);
                break;
            }
        }
    }
    selectedRuleIds = kept;
}

void RulesEditorView::ClearFilters() {
    search.clear();
    categoryFilterId = 0;
    typeFilter.clear();
    activeOnly = false;
    severityFilter = -1;
    RebuildFiltered();
}

void RulesEditorView::SelectAllFiltered() {
    selectedRuleIds.clear();
    for (const Rule& rule : filtered) selectedRuleIds.push_back(rule.id);
}

void RulesEditorView::DeleteSelectedRules() {
    if (!app || !app->HasDatabase()) return;
    if (selectedRuleIds.empty()) {
        app->ShowError("Нет отмеченных правил — отметьте строки в таблице");
        return;
    }

    int ruleTerms = 0;
    int ruleFlags = 0;
    for (int64_t ruleId : selectedRuleIds) {
        ruleTerms += app->Db().CountRuleTerms(ruleId);
        ruleFlags += app->Db().CountRuleFlags(ruleId);
    }

    std::string modalId = "bulk_delete_rules_" + std::to_string(selectedRuleIds.size());
    if (ImGui::Button((ICON_FA_TRASH "  Удалить отмеченные правила (" +
                       std::to_string(selectedRuleIds.size()) + ")").c_str())) {
        ImGui::OpenPopup(modalId.c_str());
    }
    if (ConfirmationModal(modalId, "Удалить правила",
                          "Удалить " + std::to_string(selectedRuleIds.size()) + " правил, их термины (" +
                          std::to_string(ruleTerms) + ") и отметки позиций (" +
                          std::to_string(ruleFlags) + ")? После этого можно импортировать новые данные. "
                          "Отменить это нельзя.",
                          "Удалить", "Отмена")) {
        int removedTerms = 0;
        int removedFlags = 0;
        int removed = app->Db().DeleteRules(selectedRuleIds, removedTerms, removedFlags);
        if (removed > 0) {
            app->MarkRulesChanged();
            app->SetStatus("Удалено правил: " + std::to_string(removed) +
                           ", терминов: " + std::to_string(removedTerms) +
                           ", отметок: " + std::to_string(removedFlags) +
                           " — можно импортировать новый справочник");
            selectedRuleIds.clear();
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить правила");
        }
    }
}

void RulesEditorView::RenderBulkOperations() {
    if (!app || !app->HasDatabase()) return;

    ImGui::Text(ICON_FA_USERS "  Групповые операции");
    ImGui::TextDisabled("Отметьте правила в таблице — операции применяются только к отмеченным");
    if (ImGui::Button(ICON_FA_CHECK_DOUBLE "  Отметить все по фильтру")) {
        SelectAllFiltered();
        app->SetStatus("Отмечено правил: " + std::to_string(selectedRuleIds.size()));
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "  Снять отметки")) selectedRuleIds.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("Отмечено: %zu", selectedRuleIds.size());
    if (!selectedRuleIds.empty()) {
        ImGui::SameLine();
        DeleteSelectedRules();
    }
    ImGui::Separator();
}

void RulesEditorView::RebuildFiltered() {
    filtered.clear();
    const std::string needle = ToLowerUtf8(search);

    for (const Rule& rule : rules) {
        if (activeOnly && !rule.active) continue;
        if (categoryFilterId != 0 && rule.categoryId != categoryFilterId) continue;
        if (!typeFilter.empty() && rule.type != typeFilter) continue;
        if (severityFilter >= 0 && rule.severity != severityFilter) continue;

        if (!needle.empty()) {
            bool hit = ContainsUtf8(rule.code, needle) ||
                       ContainsUtf8(rule.category, needle) ||
                       ContainsUtf8(rule.pattern, needle) ||
                       ContainsUtf8(rule.rationale, needle) ||
                       ContainsUtf8(rule.sourceDoc, needle);
            if (!hit) continue;
        }

        filtered.push_back(rule);
    }

    if (filtered.empty()) {
        selectedId = 0;
        page = 0;
        return;
    }

    bool found = false;
    for (const Rule& rule : filtered) {
        if (rule.id == selectedId) {
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

int RulesEditorView::SelectedIndex() const {
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i].id == selectedId) return static_cast<int>(i);
    }
    return -1;
}

ImVec4 RulesEditorView::CategoryColor(int64_t id) const {
    for (const Category& cat : categories) {
        if (cat.id == id) return ParseHexColor(cat.color);
    }
    return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
}

void RulesEditorView::Select(int64_t id) {
    selectedId = id;
    int idx = SelectedIndex();
    if (idx >= 0) page = idx / pageSize;
    LoadDraft();
}

void RulesEditorView::Navigate(int delta) {
    if (filtered.empty()) return;
    int idx = SelectedIndex();
    if (idx < 0) idx = 0;
    idx += delta;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(filtered.size())) idx = static_cast<int>(filtered.size()) - 1;
    Select(filtered[static_cast<size_t>(idx)].id);
}

void RulesEditorView::LoadDraft() {
    int idx = SelectedIndex();
    if (idx >= 0) {
        draft = filtered[static_cast<size_t>(idx)];
    } else {
        draft = Rule();
        draft.active = true;
        draft.type = "structural";
        draft.severity = 1;
    }
}

void RulesEditorView::SaveDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранной записи — нажмите «Добавить»");
        return;
    }
    if (draft.code.empty() || draft.categoryId == 0 || draft.type.empty()) {
        app->ShowError("Код, категория и тип правила обязательны");
        return;
    }
    if (draft.severity < 0) draft.severity = 0;
    if (draft.severity > 3) draft.severity = 3;

    PatternParts parts = ParseRulePattern(draft.pattern);
    for (const std::string& code : parts.subaccounts) {
        if (!LooksLikeSubaccount(code)) {
            app->ShowError("Субсчёт «" + code + "» выглядит неверно — ожидается вид 105.31");
            return;
        }
    }
    std::string keyword = KeywordName(parts.body);
    if (draft.type == "structural" && !IsEnumValue(keyword, StructuralKeywords())) {
        app->ShowError("Для типа structural нужна одна из проверок: arith, qty_missing, qty_mismatch, negative, no_turnover, zero_qty");
        return;
    }
    if (draft.type == "exact" && !IsEnumValue(keyword, ExactKeywords())) {
        app->ShowError("Для типа exact нужен signature или name");
        return;
    }
    if (draft.type == "synonym" && parts.body.empty()) {
        app->ShowError("Для типа synonym нужен список синонимов через запятую");
        return;
    }
    if (draft.type == "regex" && !parts.body.empty()) {
        try {
            std::regex expression(parts.body, std::regex::ECMAScript);
            (void)expression;
        } catch (const std::regex_error& error) {
            app->ShowError("Регулярное выражение некорректно: " + std::string(error.what()));
            return;
        }
    }
    draft.pattern = BuildRulePattern(parts);

    draft.id = selectedId;
    if (app->Db().UpdateRule(draft)) {
        app->MarkRulesChanged();
        app->SetStatus("Правило обновлено: " + draft.code);
        RefreshData();
        Select(selectedId);
    } else {
        app->ShowError("Не удалось обновить правило");
    }
}

void RulesEditorView::AddDraft() {
    if (!app || !app->HasDatabase()) return;
    int64_t id = app->Db().CreateBlankRule(categoryFilterId);
    if (id == 0) {
        app->ShowError("Не удалось создать правило");
        return;
    }
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Создано новое правило (ID " + std::to_string(id) + ") — заполните поля и сохраните");
    RefreshData();
    Select(id);
}

void RulesEditorView::DuplicateDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранной записи — выберите правило, которое нужно скопировать");
        return;
    }
    std::string sourceCode = draft.code;
    int64_t id = app->Db().DuplicateRule(selectedId);
    if (id == 0) {
        app->ShowError("Не удалось скопировать правило");
        return;
    }
    Rule copy;
    app->Db().GetRule(id, copy);
    highlightId = id;
    app->MarkRulesChanged();
    std::string note = "Скопировано правило " + sourceCode + " → " + copy.code +
                       " (ID " + std::to_string(id) + ") — поправьте шаблон и сохраните";
    if (copy.type == "term") {
        note += ". Список терминов не копируется — заполните его в форме «Термины»";
    }
    app->SetStatus(note);
    RefreshData();
    Select(id);
}

void RulesEditorView::DeleteSelected() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        if (ImGui::Button(ICON_FA_TRASH "  Удалить")) {
            app->ShowError("Нет выбранной записи — выберите правило в таблице");
        }
        return;
    }
    std::string modalId = "delete_rule_" + std::to_string(selectedId);
    if (ImGui::Button(ICON_FA_TRASH "  Удалить")) ImGui::OpenPopup(modalId.c_str());
    if (ConfirmationModal(modalId, "Удалить правило",
                         "Удалить правило «" + draft.code + "» и все отметки по нему? "
                         "Отменить это нельзя.", "Удалить", "Отмена")) {
        if (app->Db().DeleteRule(selectedId)) {
            app->MarkRulesChanged();
            app->SetStatus("Правило удалено: " + draft.code);
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить правило");
        }
    }
}

void RulesEditorView::RenderFilters() {
    std::vector<int64_t> catIds;
    std::vector<std::string> catNames;
    for (const Category& cat : categories) {
        catIds.push_back(cat.id);
        catNames.push_back(cat.name);
    }

    int catIndex = -1;
    int typeIndex = 0;
    int sevIndex = 0;
    std::string catText;
    for (size_t i = 0; i < catIds.size(); ++i) {
        if (catIds[i] == categoryFilterId) {
            catIndex = static_cast<int>(i);
            break;
        }
    }

    ImGui::Text(ICON_FA_FILTER "  Фильтры");
    if (ImGui::BeginTable("rules_filters", 3, ImGuiTableFlags_Borders)) {
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
            catIndex = -1;
            typeIndex = 0;
            sevIndex = 0;
        }

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        int64_t selectedCategoryId = catIndex >= 0 ? catIds[static_cast<size_t>(catIndex)] : categoryFilterId;
        ImVec4 filterColor = CategoryColor(selectedCategoryId);
        ImGui::SetNextItemWidth(FitItemWidth("Категория"));
        ComboWithFilter("Категория", catNames, catIndex, catText, &filterColor);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Тип"));
        ImGui::Combo("Тип", &typeIndex, RuleTypeFilterNames, 6);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Серьёзность"));
        ImGui::Combo("Серьёзность", &sevIndex, SeverityFilterNames, 5);

        ImGui::EndTable();
    }

    categoryFilterId = CategoryFilterId(catIds, catIndex);
    typeFilter = typeIndex == 0 ? std::string() : RuleTypeFilterNames[typeIndex];
    severityFilter = sevIndex == 0 ? -1 : sevIndex - 1;

    RebuildFiltered();
}

void RulesEditorView::RenderToolbar() {
    if (selectedId == 0) {
        ImGui::TextDisabled("Запись не выбрана");
    } else {
        ImGui::Text(ICON_FA_CIRCLE_INFO "  Выбор: ID %lld — %s [%s]",
                    static_cast<long long>(selectedId), draft.code.c_str(), draft.type.c_str());
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
    ImGui::TextDisabled(
        "Копирование берёт выбранную запись: код получает суффикс «-копия», категория, тип, "
        "шаблон, основание и источник повторяются. Обязательные поля (код, категория, тип) "
        "всегда заполняются заглушками, иначе запись не создаётся.");
    ImGui::Separator();
}

void RulesEditorView::RenderTable() {
    if (filtered.empty()) {
        ImGui::TextDisabled("Нет правил по текущим фильтрам.");
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

    if (ImGui::BeginTable("rules_table", 8,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Выбор", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Код", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Тип", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Шаблон", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Серьёз.", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Актив", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (int i = start; i < end; ++i) {
            const Rule& rule = filtered[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(rule.id));

            bool isNew = rule.id == highlightId;
            if (isNew) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
            }

            ImGui::TableNextColumn();
            bool checked = IsRuleSelected(rule.id);
            if (ImGui::Checkbox("##rule_pick", &checked)) {
                ToggleRule(rule.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(rule.id).c_str(), selectedId == rule.id)) {
                Select(rule.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(rule.code.c_str(), selectedId == rule.id)) {
                Select(rule.id);
            }

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, CategoryColor(rule.categoryId));
            if (ImGui::Selectable(rule.category.empty() ? " " : rule.category.c_str(), selectedId == rule.id)) {
                Select(rule.id);
            }
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            if (ImGui::Selectable(rule.type.c_str(), selectedId == rule.id)) {
                Select(rule.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(rule.pattern.empty() ? " " : rule.pattern.c_str(), selectedId == rule.id)) {
                Select(rule.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(rule.severity).c_str(), selectedId == rule.id)) {
                Select(rule.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(rule.active ? "да" : "нет", selectedId == rule.id)) {
                Select(rule.id);
            }

            if (isNew) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void RulesEditorView::RenderForm() {
    ImGui::Text(ICON_FA_PENCIL "  Редактор правила");
    if (selectedId == 0) {
        ImGui::TextDisabled("Запись не выбрана — нажмите «Добавить» над таблицей");
        return;
    }
    ImGui::Text("ID: %lld", static_cast<long long>(selectedId));
    if (draft.code.rfind("новый-", 0) == 0 || draft.code.find("-копия") != std::string::npos) {
        ImGui::TextDisabled("Запись создана с заглушками — замените код и заполните шаблон, затем сохраните.");
    }
    ImGui::Separator();

    std::vector<int64_t> catIds;
    std::vector<std::string> catNames;
    for (const Category& cat : categories) {
        catIds.push_back(cat.id);
        catNames.push_back(cat.name);
    }
    int catIndex = -1;
    for (size_t i = 0; i < catIds.size(); ++i) {
        if (catIds[i] == draft.categoryId) {
            catIndex = static_cast<int>(i);
            break;
        }
    }
    std::string catText;
    int typeIndex = TypeIndex(draft.type);

    if (ImGui::BeginTable("rules_form_fields", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        InputTextSync("Код", draft.code, FitItemWidth("Код"));
        ImGui::TableNextColumn();
        int severityIndex = draft.severity < 0 ? 0 : (draft.severity > 3 ? 3 : draft.severity);
        ImGui::SetNextItemWidth(FitItemWidth("Серьёзность"));
        if (ImGui::Combo("Серьёзность", &severityIndex, SeverityNames, 4)) {
            draft.severity = severityIndex;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text(ICON_FA_TAGS "  Категория");
        ImVec4 catColor = CategoryColor(draft.categoryId);
        ColorSwatch(catColor, 14.0f);
        ImGui::SameLine();
        ComboWithFilter("", catNames, catIndex, catText, &catColor);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Тип"));
        ImGui::Combo("Тип", &typeIndex, RuleTypeNames, 5);
        ImGui::EndTable();
    }

    if (catIndex >= 0 && catIndex < static_cast<int>(catIds.size())) {
        draft.categoryId = catIds[static_cast<size_t>(catIndex)];
    }
    draft.type = RuleTypeNames[typeIndex];

    PatternParts parts = ParseRulePattern(draft.pattern);

    std::string subText = JoinSubaccounts(parts.subaccounts);
    InputTextSync("Субсчёты (sub:)", subText, FitItemWidth("Субсчёты (sub:)"));
    ImGui::TextDisabled("Через запятую, вид 105.31. Пусто — правило проверяет все субсчёты.");

    std::string body = parts.body;
    if (draft.type == "structural") {
        std::vector<std::string> structural = StructuralKeywords();
        std::string keyword = KeywordName(body);
        if (!IsEnumValue(keyword, structural)) keyword = structural[0];
        ComboEnum("Проверка", structural, keyword);
        if (keyword == "no_turnover") {
            double threshold = KeywordThreshold(body);
            AmountInput("Порог остатка", threshold);
            body = threshold > 0.0 ? keyword + ":" + FormatAmount(threshold) : keyword;
        } else {
            body = keyword;
        }
    } else if (draft.type == "exact") {
        std::vector<std::string> exact = ExactKeywords();
        std::string keyword = KeywordName(body);
        if (!IsEnumValue(keyword, exact)) keyword = exact[0];
        ComboEnum("Признак дубликата", exact, keyword);
        body = keyword;
    } else if (draft.type == "synonym") {
        InputTextSync("Синонимы (через запятую)", body, FitItemWidth("Синонимы (через запятую)"));
    } else if (draft.type == "term") {
        InputTextSync("Запасной список (обычно пусто)", body, FitItemWidth("Запасной список (обычно пусто)"));
    } else {
        InputTextSync("Регулярное выражение", body, FitItemWidth("Регулярное выражение"));
    }

    PatternParts edited;
    edited.subaccounts = SplitCommaList(subText);
    edited.body = body;

    RenderPatternHelp(draft.type, edited, body);

    edited.body = body;
    draft.pattern = BuildRulePattern(edited);

    InputTextSync("Основание", draft.rationale, FitItemWidth("Основание"));
    InputTextSync("Источник", draft.sourceDoc, FitItemWidth("Источник"));
    ImGui::Checkbox("Активно", &draft.active);

    if (draft.type == "term") {
        int count = 0;
        std::vector<std::string> preview;
        for (const Term& term : terms) {
            if (term.ruleId == draft.id) {
                ++count;
                if (preview.size() < 20) preview.push_back(term.term);
            }
        }
        ImGui::Separator();
        ImGui::Text(ICON_FA_LIST "  Список терминов правила: %d", count);
        for (const std::string& term : preview) {
            ImGui::TextDisabled("  • %s", term.c_str());
        }
        if (count > static_cast<int>(preview.size())) {
            ImGui::TextDisabled("  … ещё %d (см. форму «Термины и списки»)", count - static_cast<int>(preview.size()));
        }
    }

    ImGui::Separator();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Сохранить")) SaveDraft();
}

void RulesEditorView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }

        if (!initialized) RefreshData();

        ImGui::Text(ICON_FA_GAVEL "  Правила: %zu всего, %zu по фильтру", rules.size(), filtered.size());
        ImGui::TextDisabled("Порядок применения: structural → exact → synonym → term → regex");
        if (ImGui::Button(ICON_FA_LIGHTBULB "  Добавить примеры правил")) {
            if (app->Db().SeedExampleRules()) {
                app->MarkRulesChanged();
                app->SetStatus("Примеры правил добавлены (ОМС, неверный субсчёт, кандидаты в ОС)");
                RefreshData();
            } else {
                app->ShowError("Не удалось добавить примеры правил");
            }
        }
        RenderLogicHelp(rules);
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

        ImGui::BeginChild("rules_list", ImVec2(listWidth, height), true);
        RenderToolbar();
        RenderTable();
        ImGui::EndChild();

        ImGui::SameLine();
        VerticalSplitter("rules_split", &listWidth, minLeft, maxLeft, 10.0f, height);
        ImGui::SameLine();

        ImGui::BeginChild("rules_form", ImVec2(available - listWidth - 14.0f, height), true);
        RenderForm();
        ImGui::EndChild();

        EndWindow();
    }
}
