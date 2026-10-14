#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class RulesEditorView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;
    void DuplicateDraft();
    void ClearFilters();

private:
    std::vector<Rule> rules;
    std::vector<Category> categories;
    std::vector<Term> terms;
    std::vector<TermList> lists;
    std::vector<Rule> filtered;
    int64_t selectedId = 0;
    int64_t highlightId = 0;
    Rule draft;

    std::string search;

    int page = 0;
    int pageSize = 100;
    float listWidth = 620.0f;
    bool initialized = false;

    void RebuildFiltered();
    int SelectedIndex() const;
    ImVec4 CategoryColor(int64_t categoryId) const;
    void Select(int64_t id);
    void Navigate(int delta);
    void LoadDraft();
    void SaveDraft();
    void AddDraft();
    void DeleteSelected();
    void DeleteFilteredRules();

    void RenderFilters();
    void RenderBulkOperations();
    void RenderToolbar();
    void RenderTable();
    void RenderForm();
};
