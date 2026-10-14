#include "CategoriesEditorView.h"

#include "../App.h"
#include "../IconsFontAwesome6.h"
#include "../Widgets.h"
#include "../domain/Utf8.h"

#include <algorithm>
#include <string>
#include <vector>

void CategoriesEditorView::RefreshData() {
    if (!app || !app->HasDatabase()) return;
    categories = app->Db().ListCategories();
    rules = app->Db().ListRules();
    RebuildFiltered();
    initialized = true;
}

void CategoriesEditorView::RebuildFiltered() {
    filtered.clear();

    for (const Category& cat : categories) {
        std::vector<std::string> fields = {
            cat.name, cat.description, cat.color
        };
        if (!MatchesFilter(search, fields)) continue;
        filtered.push_back(cat);
    }

    if (filtered.empty()) {
        selectedId = 0;
        page = 0;
        return;
    }

    bool found = false;
    for (const Category& cat : filtered) {
        if (cat.id == selectedId) {
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

int CategoriesEditorView::SelectedIndex() const {
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i].id == selectedId) return static_cast<int>(i);
    }
    return -1;
}

void CategoriesEditorView::Select(int64_t id) {
    selectedId = id;
    int idx = SelectedIndex();
    if (idx >= 0) page = idx / pageSize;
    LoadDraft();
}

void CategoriesEditorView::Navigate(int delta) {
    if (filtered.empty()) return;
    int idx = SelectedIndex();
    if (idx < 0) idx = 0;
    idx += delta;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(filtered.size())) idx = static_cast<int>(filtered.size()) - 1;
    Select(filtered[static_cast<size_t>(idx)].id);
}

void CategoriesEditorView::LoadDraft() {
    int idx = SelectedIndex();
    if (idx >= 0) {
        draft = filtered[static_cast<size_t>(idx)];
    } else {
        draft = Category();
        draft.color = "#666666";
        draft.active = true;
    }
}

void CategoriesEditorView::SaveDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранной записи — нажмите «Добавить»");
        return;
    }
    if (draft.name.empty()) {
        app->ShowError("Название категории обязательно");
        return;
    }
    for (const Category& other : categories) {
        if (other.id != selectedId && EqualsUtf8Insensitive(other.name, draft.name)) {
            app->ShowError("Категория «" + draft.name + "» уже существует — выберите другое название");
            return;
        }
    }
    std::string color = NormalizeHexColor(draft.color);
    if (color.empty()) {
        app->ShowError("Цвет категории должен быть в формате #RRGGBB");
        return;
    }
    draft.color = color;

    draft.id = selectedId;
    if (app->Db().UpdateCategory(draft)) {
        app->MarkRulesChanged();
        app->SetStatus("Категория обновлена: " + draft.name);
        RefreshData();
        Select(selectedId);
    } else {
        app->ShowError("Не удалось обновить категорию");
    }
}

void CategoriesEditorView::AddDraft() {
    if (!app || !app->HasDatabase()) return;
    int64_t id = app->Db().CreateBlankCategory();
    if (id == 0) {
        app->ShowError("Не удалось создать категорию");
        return;
    }
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Создана новая категория (ID " + std::to_string(id) + ") — заполните поля и сохраните");
    RefreshData();
    Select(id);
}

void CategoriesEditorView::DuplicateDraft() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        app->ShowError("Нет выбранной записи — выберите категорию, которую нужно скопировать");
        return;
    }
    std::string sourceName = draft.name;
    int64_t id = app->Db().DuplicateCategory(selectedId);
    if (id == 0) {
        app->ShowError("Не удалось скопировать категорию");
        return;
    }
    Category copy;
    app->Db().GetCategory(id, copy);
    highlightId = id;
    app->MarkRulesChanged();
    app->SetStatus("Скопирована категория «" + sourceName + "» → «" + copy.name + "» (ID " +
                   std::to_string(id) + ") — поправьте поля и сохраните");
    RefreshData();
    Select(id);
}

void CategoriesEditorView::DeleteSelected() {
    if (!app || !app->HasDatabase()) return;
    if (selectedId == 0) {
        if (ImGui::Button(ICON_FA_TRASH "  Удалить")) {
            app->ShowError("Нет выбранной записи — выберите категорию в таблице");
        }
        return;
    }
    std::string modalId = "delete_category_" + std::to_string(selectedId);
    int movedRules = 0;
    for (const Rule& rule : rules) {
        if (EqualsUtf8Insensitive(rule.category, draft.name)) ++movedRules;
    }
    if (ImGui::Button(ICON_FA_TRASH "  Удалить")) ImGui::OpenPopup(modalId.c_str());
    if (ConfirmationModal(modalId, "Удалить категорию",
                         "Удалить категорию «" + draft.name + "»? "
                         "Правила из неё (" + std::to_string(movedRules) + ") будут перенесены "
                         "в другую категорию. Отменить это нельзя.",
                         "Удалить", "Отмена")) {
        if (app->Db().DeleteCategory(selectedId)) {
            app->MarkRulesChanged();
            app->SetStatus("Категория удалена, правил перенесено: " + std::to_string(movedRules));
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить категорию");
        }
    }
}

void CategoriesEditorView::DeleteFilteredCategories() {
    if (!app || !app->HasDatabase()) return;
    if (filtered.empty()) {
        app->ShowError("По текущему фильтру нет категорий — измените фильтр");
        return;
    }

    int movedRules = 0;
    for (const Category& category : filtered) {
        for (const Rule& rule : rules) {
            if (EqualsUtf8Insensitive(rule.category, category.name)) ++movedRules;
        }
    }

    std::string modalId = "bulk_delete_categories_" + std::to_string(filtered.size());
    if (ImGui::Button((ICON_FA_TRASH "  Удалить категории по фильтру (" +
                       std::to_string(filtered.size()) + ")").c_str())) {
        ImGui::OpenPopup(modalId.c_str());
    }
    if (ConfirmationModal(modalId, "Удалить категории по фильтру",
                         "Удалить " + std::to_string(filtered.size()) + " категорий? Правила из них (" +
                         std::to_string(movedRules) + ") будут перенесены в другую категорию. "
                         "Отменить это нельзя.",
                         "Удалить", "Отмена")) {
        int removed = 0;
        for (const Category& category : filtered) {
            if (app->Db().DeleteCategory(category.id)) ++removed;
        }
        if (removed > 0) {
            app->MarkRulesChanged();
            app->SetStatus("Удалено категорий: " + std::to_string(removed) +
                           " — правила перенесены в другую категорию");
            selectedId = 0;
            highlightId = 0;
            RefreshData();
        } else {
            app->ShowError("Не удалось удалить категории");
        }
    }
}

void CategoriesEditorView::RenderBulkOperations() {
    if (!app || !app->HasDatabase()) return;

    if (!ImGui::CollapsingHeader(ICON_FA_USERS "  Групповые операции")) return;

    ImGui::TextDisabled("Операции применяются ко всем категориям, прошедшим фильтр. Удаление необратимо.");
    DeleteFilteredCategories();
}

void CategoriesEditorView::RenderFilters() {
    ImGui::Text(ICON_FA_FILTER "  Фильтр");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextDisabled("Фразы через запятую — И; внутри фразы поиск по всем полям — ИЛИ");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.24f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.95f, 1.0f, 1.0f));
    ImGui::InputText("##categories_filter", &search, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ERASER "  Очистить")) {
        search.clear();
    }
    ImGui::Separator();
    RebuildFiltered();
}

void CategoriesEditorView::RenderToolbar() {
    if (selectedId == 0) {
        ImGui::TextDisabled("Запись не выбрана");
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

void CategoriesEditorView::RenderTable() {
    if (filtered.empty()) {
        ImGui::TextDisabled("Нет категорий по текущим фильтрам.");
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

    if (ImGui::BeginTable("categories_table", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Название", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Цвет", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Описание", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Правил", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Порядок", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Актив", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (int i = start; i < end; ++i) {
            const Category& category = filtered[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(category.id));

            bool isNew = category.id == highlightId;
            if (isNew) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(category.id).c_str(), selectedId == category.id)) {
                Select(category.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(category.name.empty() ? " " : category.name.c_str(), selectedId == category.id)) {
                Select(category.id);
            }

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ParseHexColor(category.color));
            if (ImGui::Selectable(category.color.c_str(), selectedId == category.id)) {
                Select(category.id);
            }
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            if (ImGui::Selectable(category.description.empty() ? " " : category.description.c_str(), selectedId == category.id)) {
                Select(category.id);
            }

            ImGui::TableNextColumn();
            int ruleCount = 0;
            for (const Rule& rule : rules) {
                if (EqualsUtf8Insensitive(rule.category, category.name)) ++ruleCount;
            }
            if (ImGui::Selectable(std::to_string(ruleCount).c_str(), selectedId == category.id)) {
                Select(category.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(category.sortOrder).c_str(), selectedId == category.id)) {
                Select(category.id);
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(category.active ? "да" : "нет", selectedId == category.id)) {
                Select(category.id);
            }

            if (isNew) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void CategoriesEditorView::RenderForm() {
    ImGui::Text(ICON_FA_TAGS "  Редактор категории");
    if (selectedId == 0) {
        ImGui::TextDisabled("Запись не выбрана — нажмите «Добавить» над таблицей");
        return;
    }
    ImGui::Text("ID: %lld", static_cast<long long>(selectedId));
    ImGui::Separator();

    if (ImGui::BeginTable("categories_form_fields", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        InputTextSync("Название", draft.name, FitItemWidth("Название"));
        ImGui::TableNextColumn();
        ImGui::Text(ICON_FA_PALETTE "  Цвет");
        ColorPalettePicker("category_color_palette", draft.color, 70.0f);
        ImGui::SameLine();
        InputTextSync("", draft.color, ImGui::GetContentRegionAvail().x);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(FitItemWidth("Порядок"));
        ImGui::InputInt("Порядок", &draft.sortOrder, 0, 1);
        ImGui::TableNextColumn();
        ImGui::Checkbox("Активна", &draft.active);

        ImGui::EndTable();
    }

    InputTextSync("Описание", draft.description, FitItemWidth("Описание"));

    ImGui::Separator();
    int ruleCount = 0;
    for (const Rule& rule : rules) {
        if (EqualsUtf8Insensitive(rule.category, draft.name)) ++ruleCount;
    }
    ImGui::Text(ICON_FA_GAVEL "  Правил в категории: %d", ruleCount);
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Сохранить")) SaveDraft();
}

void CategoriesEditorView::Render() {
    if (!app) return;

    if (BeginWindow()) {
        if (!app->HasDatabase()) {
            ImGui::TextDisabled("База не открыта.");
            EndWindow();
            return;
        }

        if (!initialized) RefreshData();

        ImGui::Text(ICON_FA_TAGS "  Категории: %zu всего, %zu по фильтру", categories.size(), filtered.size());
        ImGui::TextDisabled("Категории используются правилами и отчётами.");
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

        ImGui::BeginChild("categories_list", ImVec2(listWidth, height), true);
        RenderToolbar();
        RenderTable();
        ImGui::EndChild();

        ImGui::SameLine();
        VerticalSplitter("categories_split", &listWidth, minLeft, maxLeft, 10.0f, height);
        ImGui::SameLine();

        ImGui::BeginChild("categories_form", ImVec2(available - listWidth - 14.0f, height), true);
        RenderForm();
        ImGui::EndChild();

        EndWindow();
    }
}
