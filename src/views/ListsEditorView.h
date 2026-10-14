#pragma once

#include "BaseView.h"
#include "../Database.h"

#include <string>
#include <vector>

class ListsEditorView : public BaseView {
public:
    void Render() override;
    void RefreshData() override;
    void DuplicateDraft();

private:
    std::vector<TermList> lists;
    std::vector<Rule> rules;
    std::vector<Term> terms;
    std::vector<TermList> filtered;
    int64_t selectedId = 0;
    int64_t highlightId = 0;
    TermList draft;

    std::string search;

    std::string bulkText;
    std::string bulkSource;
    std::string bulkDate;

    int page = 0;
    int pageSize = 100;
    float listWidth = 620.0f;
    bool initialized = false;

    void RebuildFiltered();
    int SelectedIndex() const;
    void Select(int64_t id);
    void Navigate(int delta);
    void LoadDraft();
    void SaveDraft();
    void AddDraft();
    void DeleteSelected();
    void DeleteFilteredLists();
    void AddTermsToDraft();

    std::vector<Term> DraftTerms() const;

    void RenderFilters();
    void RenderBulkOperations();
    void RenderToolbar();
    void RenderTable();
    void RenderForm();
};
