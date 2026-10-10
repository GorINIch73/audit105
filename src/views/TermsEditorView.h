#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class TermsEditorView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;
    void DuplicateDraft();
    void DuplicateSelectedList();
    void SelectList(int64_t ruleId, const std::string& listName);
    void SelectAllFiltered();
    void ClearFilters();
    size_t SelectedTermCount() const { return selectedTermIds.size(); }
    size_t SelectedListCount() const { return selectedListRuleIds.size(); }

private:
    std::vector<Term> terms;
    std::vector<Rule> rules;
    std::vector<TermList> lists;
    std::vector<Term> filtered;
    std::vector<TermList> filteredLists;
    int64_t selectedId = 0;
    int64_t highlightId = 0;
    Term draft;

    std::string search;
    int64_t ruleFilterId = 0;
    std::string listFilter;
    std::string originFilter;
    std::string scopeFilter;
    bool activeOnly = false;

    std::string bulkText;
    std::string bulkSource;
    std::string bulkDate;
    int64_t selectedListRuleId = 0;

    std::vector<int64_t> selectedTermIds;
    std::vector<int64_t> selectedListRuleIds;
    std::vector<std::string> selectedListNames;

    int page = 0;
    int pageSize = 100;
    float listWidth = 620.0f;
    bool initialized = false;

    void RebuildFiltered();
    void RebuildFilteredLists();
    int SelectedIndex() const;
    void Select(int64_t id);
    void Navigate(int delta);
    void LoadDraft();
    void SaveDraft();
    void AddDraft();
    void DeleteSelected();

    bool IsTermSelected(int64_t id) const;
    bool IsListSelected(int64_t ruleId, const std::string& listName) const;
    void ToggleTerm(int64_t id);
    void ToggleList(int64_t ruleId, const std::string& listName);
    void PruneSelection();
    std::vector<int64_t> SelectedRuleIds() const;
    void DeleteSelectedRules();

    void RenderBulkOperations();
    void RenderLists();
    void RenderFilters();
    void RenderToolbar();
    void RenderTable();
    void RenderForm();
};
