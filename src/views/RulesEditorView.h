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
    void SelectAllFiltered();
    void ClearFilters();
    size_t SelectedRuleCount() const { return selectedRuleIds.size(); }

private:
    std::vector<Rule> rules;
    std::vector<Category> categories;
    std::vector<Term> terms;
    std::vector<Rule> filtered;
    int64_t selectedId = 0;
    int64_t highlightId = 0;
    Rule draft;

    std::string search;
    int64_t categoryFilterId = 0;
    std::string typeFilter;
    bool activeOnly = false;
    int severityFilter = -1;

    std::vector<int64_t> selectedRuleIds;

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
    bool IsRuleSelected(int64_t id) const;
    void ToggleRule(int64_t id);
    void PruneSelection();
    void DeleteSelectedRules();

    void RenderFilters();
    void RenderBulkOperations();
    void RenderToolbar();
    void RenderTable();
    void RenderForm();
};
