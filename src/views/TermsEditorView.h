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
    void ClearFilters();

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
    std::string listFilter;
    bool onlyImport = false;
    int staleMonths = 12;
    int importedCount = 0;
    int staleCount = 0;

    std::string bulkText;
    std::string bulkSource;
    std::string bulkDate;
    int64_t selectedListRuleId = 0;

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

    void RenderBulkOperations();
    void RenderLists();
    void RenderFilters();
    void RenderToolbar();
    void RenderTable();
    void RenderForm();
};
