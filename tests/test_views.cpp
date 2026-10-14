#include "imgui.h"
#include "imgui_internal.h"

#include "App.h"
#include "Widgets.h"
#include "views/FormView.h"
#include "views/ImportView.h"
#include "views/ReportView.h"
#include "views/RulesView.h"
#include "views/RulesEditorView.h"
#include "views/CategoriesEditorView.h"
#include "views/TermsEditorView.h"
#include "views/ListsEditorView.h"
#include "views/RunsView.h"
#include "views/SettingsView.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <utility>
#include <string>

static void Frame(App& app) {
    ImGui::NewFrame();
    app.RenderMenuBar();
    app.RenderViews();
    ImGui::Render();
}

static bool HasView(App& app, const std::string& title) {
    for (auto& view : app.views) {
        std::string t = view->GetTitle();
        size_t cut = t.find("###");
        if (cut != std::string::npos) t = t.substr(0, cut);
        if (t == title) return true;
    }
    return false;
}

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);

    App app;
    app.ApplyTheme(0);
    unsigned char* pixels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texW, &texH);

    BaseView* form = app.CreateView<FormView>("Форма МЗ");
    BaseView* settings = app.CreateView<SettingsView>();
    printf("ok: created %zu views\n", app.views.size());
    if (app.views.size() != 2) return 1;

    for (int i = 0; i < 3; ++i) Frame(app);
    if (app.views.size() != 2) { printf("fail: views vanished while open\n"); return 1; }
    if (!form->IsOpen || !settings->IsOpen) { printf("fail: views not open\n"); return 1; }
    printf("ok: views stay open across frames\n");

    // 1) RequestClose (крестик в заголовке / Ctrl+W)
    form->RequestClose();
    Frame(app);
    if (app.views.size() != 1 || HasView(app, "Форма МЗ")) {
        printf("fail: RequestClose did not close the form (%zu)\n", app.views.size());
        return 1;
    }
    printf("ok: RequestClose closed the form\n");

    // 2) закрытие через меню (App::CloseView)
    app.CloseView(settings);
    Frame(app);
    if (!app.views.empty()) { printf("fail: CloseView did not remove the view\n"); return 1; }
    printf("ok: CloseView removed the view\n");

    // 3) Ctrl+W закрывает активную форму
    app.CreateView<ImportView>("Импорт оборота");
    app.CreateView<FormView>("Форма МЗ");
    Frame(app);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_W, true);
    Frame(app);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_W, false);
    if (app.views.size() != 1) {
        printf("fail: Ctrl+W closed %zu views instead of 1\n", app.views.size());
        return 1;
    }
    printf("ok: Ctrl+W closed the focused form\n");

    // 4) «Закрыть все формы»
    app.CloseAllViews();
    Frame(app);
    if (!app.views.empty()) { printf("fail: CloseAllViews left views behind\n"); return 1; }
    printf("ok: CloseAllViews cleared everything\n");

    // 5) повторное открытие той же формы работает
    app.CreateView<FormView>("Форма МЗ");
    Frame(app);
    if (app.views.size() != 1) { printf("fail: reopened view missing\n"); return 1; }
    printf("ok: reopened form after closing\n");

    // 6) справочники и отчёт открываются, переключаются вкладки
    const std::string dbPath = "/tmp/audit105_views.db";
    std::remove(dbPath.c_str());
    std::remove((dbPath + "-wal").c_str());
    std::remove((dbPath + "-shm").c_str());
    if (!app.OpenDatabase(dbPath)) { printf("fail: cannot open test db\n"); return 1; }

    app.CloseAllViews();

    RulesEditorView* rulesEditor = app.CreateView<RulesEditorView>("Правила");
    CategoriesEditorView* categoriesEditor = app.CreateView<CategoriesEditorView>("Категории");
    TermsEditorView* termsEditor = app.CreateView<TermsEditorView>("Термины");
    ListsEditorView* listsEditor = app.CreateView<ListsEditorView>("Перечни");
    RulesView* rulesSummary = app.CreateView<RulesView>("Сводка правил");
    rulesSummary->OpenTab(0);
    app.CreateView<ReportView>("Отчёт");

    for (int i = 0; i < 3; ++i) Frame(app);
    if (app.views.size() != 6) {
        printf("fail: dictionary/report views not open (%zu)\n", app.views.size());
        for (auto& view : app.views) printf("  remaining: %s\n", view->GetTitle());
        return 1;
    }
    if (!rulesEditor->IsOpen || !categoriesEditor->IsOpen || !termsEditor->IsOpen ||
        !listsEditor->IsOpen || !rulesSummary->IsOpen) {
        printf("fail: dictionary editor views not open\n");
        return 1;
    }
    printf("ok: separate dictionary editor views render\n");

    auto findWindow = [](const char* name) -> ImGuiWindow* {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        for (ImGuiWindow* w : ctx->Windows) {
            if (strstr(w->Name, name) != nullptr) return w;
        }
        return nullptr;
    };
    const std::pair<const char*, const char*> pairs[] = {
        {"rules_list", "rules_form"},
        {"categories_list", "categories_form"},
        {"terms_list", "terms_form"},
        {"lists_list", "lists_form"},
    };
    for (const auto& p : pairs) {
        ImGuiWindow* left = findWindow(p.first);
        ImGuiWindow* right = findWindow(p.second);
        if (!left || !right) {
            printf("fail: editor panel %s missing\n", p.second);
            return 1;
        }
        if (right->Size.x < 100.0f || right->Size.y < 100.0f) {
            printf("fail: editor panel %s is degenerate (%.0fx%.0f)\n", p.second,
                   right->Size.x, right->Size.y);
            return 1;
        }
        if (right->Pos.x <= left->Pos.x + left->Size.x) {
            printf("fail: editor panel %s overlaps the table\n", p.second);
            return 1;
        }
        if (fabs(right->Pos.y - left->Pos.y) > 2.0f) {
            printf("fail: editor panel %s is not aligned with the table\n", p.second);
            return 1;
        }
    }
    printf("ok: table and editor panels sit side by side\n");

    for (const char* title : {"Правила", "Категории", "Термины"}) {
        ImGuiWindow* w = findWindow(title);
        if (!w || w->Size.x < 900.0f) {
            printf("fail: editor window %s is too narrow (%.0f)\n",
                   title, w ? w->Size.x : 0.0f);
            return 1;
        }
    }
    for (const char* name : {"rules_form", "categories_form", "terms_form"}) {
        ImGuiWindow* w = findWindow(name);
        if (!w || w->Size.x < 260.0f) {
            printf("fail: editor panel %s is too narrow (%.0f)\n", name, w ? w->Size.x : 0.0f);
            return 1;
        }
    }
    printf("ok: editor windows keep a usable minimum size\n");

    // 7а) «Добавить копированием» дублирует выбранное правило
    rulesEditor->RefreshData();
    size_t rulesBeforeCopy = app.Db().ListRules().size();
    rulesEditor->DuplicateDraft();
    size_t rulesAfterCopy = app.Db().ListRules().size();
    if (rulesAfterCopy != rulesBeforeCopy + 1) {
        printf("fail: copy-to-add created %zu rules instead of one\n", rulesAfterCopy - rulesBeforeCopy);
        return 1;
    }
    int64_t copyId = 0;
    for (const Rule& rule : app.Db().ListRules()) {
        if (rule.code.find("-копия") != std::string::npos) copyId = rule.id;
    }
    if (copyId == 0) {
        printf("fail: copied rule has no '-копия' code\n");
        return 1;
    }
    app.Db().DeleteRule(copyId);
    printf("ok: copy-to-add duplicates the selected rule\n");

    // 7а2) «Добавить копированием» дублирует выбранную категорию
    categoriesEditor->RefreshData();
    size_t catsBeforeCopy = app.Db().ListCategories().size();
    categoriesEditor->DuplicateDraft();
    size_t catsAfterCopy = app.Db().ListCategories().size();
    if (catsAfterCopy != catsBeforeCopy + 1) {
        printf("fail: copy-to-add created %zu categories instead of one\n", catsAfterCopy - catsBeforeCopy);
        return 1;
    }
    int64_t catCopyId = 0;
    for (const Category& cat : app.Db().ListCategories()) {
        if (cat.name.find("(копия)") != std::string::npos) catCopyId = cat.id;
    }
    if (catCopyId == 0) {
        printf("fail: copied category has no '(копия)' name\n");
        return 1;
    }
    app.Db().DeleteCategory(catCopyId);
    printf("ok: copy-to-add duplicates the selected category\n");

    // 7а3) «Добавить копированием» дублирует выбранный термин
    termsEditor->RefreshData();
    size_t termsBeforeCopy = app.Db().ListTerms().size();
    termsEditor->DuplicateDraft();
    size_t termsAfterCopy = app.Db().ListTerms().size();
    if (termsAfterCopy != termsBeforeCopy + 1) {
        printf("fail: copy-to-add created %zu terms instead of one\n", termsAfterCopy - termsBeforeCopy);
        return 1;
    }
    int64_t termCopyId = 0;
    for (const Term& term : app.Db().ListTerms()) {
        if (term.term.find("(копия)") != std::string::npos) termCopyId = term.id;
    }
    if (termCopyId == 0) {
        printf("fail: copied term has no '(копия)' name\n");
        return 1;
    }
    app.Db().DeleteTerm(termCopyId);
    printf("ok: copy-to-add duplicates the selected term\n");

    // 7а4) копирование списка терминов
    int64_t listRuleId = 0;
    std::string listRule;
    std::string listName;
    for (const TermList& list : app.Db().ListTermLists()) {
        if (!list.name.empty() && list.termCount > 0) {
            listRuleId = list.ruleId;
            listRule = list.ruleCode;
            listName = list.name;
            break;
        }
    }
    if (listName.empty()) {
        printf("fail: no term list available to copy\n");
        return 1;
    }
    termsEditor->RefreshData();
    termsEditor->SelectList(listRuleId, listName);
    termsEditor->DuplicateSelectedList();
    std::string copiedName = listName + " (копия)";
    int copiedCount = 0;
    for (const TermList& list : app.Db().ListTermLists()) {
        if (list.ruleCode == listRule && list.name == copiedName) copiedCount = list.termCount;
    }
    if (copiedCount == 0) {
        printf("fail: copied term list '%s' is empty\n", copiedName.c_str());
        return 1;
    }
    for (const Term& term : app.Db().ListTerms()) {
        if (term.ruleCode == listRule && term.listName == copiedName) app.Db().DeleteTerm(term.id);
    }
    printf("ok: copy-to-add duplicates the selected term list (%d terms)\n", copiedCount);

    // 7а5) форма «Перечни»: копирование перечня вместе с терминами
    listsEditor->RefreshData();
    std::vector<int64_t> listIdsBefore;
    for (const TermList& list : app.Db().ListTermLists()) listIdsBefore.push_back(list.id);
    int listsBeforeCopy = static_cast<int>(listIdsBefore.size());
    listsEditor->DuplicateDraft();
    std::vector<TermList> newLists;
    for (const TermList& list : app.Db().ListTermLists()) {
        bool seen = false;
        for (int64_t id : listIdsBefore) {
            if (id == list.id) {
                seen = true;
                break;
            }
        }
        if (!seen) newLists.push_back(list);
    }
    if (newLists.size() != 1) {
        printf("fail: lists editor copy did not create a list (%d -> %zu new)\n",
               listsBeforeCopy, newLists.size());
        return 1;
    }
    if (newLists.front().name.find("копия") == std::string::npos) {
        printf("fail: copied list name has no '(копия)' suffix\n");
        return 1;
    }
    for (const TermList& list : newLists) {
        for (const Term& term : app.Db().ListTerms()) {
            if (term.ruleId == list.ruleId && term.listName == list.name) app.Db().DeleteTerm(term.id);
        }
        app.Db().DeleteTermList(list.id);
    }
    if (static_cast<int>(app.Db().ListTermLists().size()) != listsBeforeCopy) {
        printf("fail: copied lists not cleaned up\n");
        return 1;
    }
    printf("ok: lists editor duplicates the selected term list\n");

    // 7а6) групповые операции: применяются по фильтру с подтверждением
    int64_t groupCategoryId = 0;
    for (const Category& cat : app.Db().ListCategories()) {
        if (cat.name == "Обратить внимание") {
            groupCategoryId = cat.id;
            break;
        }
    }
    int64_t groupRule = app.Db().CreateBlankRule(groupCategoryId);
    app.Db().AddTermsToList(groupRule, "Групповой перечень", {"групповой термин один"});
    rulesEditor->RefreshData();
    size_t rulesBeforeBulk = app.Db().ListRules().size();
    for (int i = 0; i < 3; ++i) Frame(app);
    if (app.Db().ListRules().size() != rulesBeforeBulk) {
        printf("fail: bulk delete removed rules without confirmation\n");
        return 1;
    }
    printf("ok: group operations require confirmation before deleting rules\n");

    termsEditor->RefreshData();
    termsEditor->ClearFilters();
    for (int i = 0; i < 3; ++i) Frame(app);
    if (app.Db().ListTerms().empty()) {
        printf("fail: bulk delete removed terms without confirmation\n");
        return 1;
    }
    printf("ok: group operations require confirmation before deleting terms\n");
    for (const Term& term : app.Db().ListTerms()) {
        if (term.ruleId == groupRule) app.Db().DeleteTerm(term.id);
    }
    app.Db().DeleteRule(groupRule);

    // 7б) критичные операции (удаление) требуют подтверждения в модальном окне
    rulesEditor->RefreshData();
    size_t rulesBeforeDelete = app.Db().ListRules().size();
    for (int i = 0; i < 3; ++i) Frame(app);
    if (ImGui::GetCurrentContext()->BeginPopupStack.Size > 0 ||
        app.Db().ListRules().size() != rulesBeforeDelete) {
        printf("fail: delete confirmation popup is open without a click\n");
        return 1;
    }

    ImGui::NewFrame();
    ImGui::Begin("modal_probe");
    bool confirmed = ConfirmationModal("probe_confirm", "Удалить правило", "Тестовое сообщение");
    ImGui::End();
    ImGui::Render();
    if (confirmed || ImGui::GetCurrentContext()->BeginPopupStack.Size > 0) {
        printf("fail: confirmation modal opens on its own\n");
        return 1;
    }

    ImGuiWindow* modal = nullptr;
    for (int i = 0; i < 3; ++i) {
        ImGui::NewFrame();
        ImGui::Begin("modal_probe");
        if (i == 0) ImGui::OpenPopup("probe_confirm");
        confirmed = ConfirmationModal("probe_confirm", "Удалить правило", "Тестовое сообщение");
        modal = ImGui::FindWindowByName("probe_confirm");
        ImGui::End();
        ImGui::Render();
    }
    if (confirmed) {
        printf("fail: confirmation modal confirmed without a click\n");
        return 1;
    }
    if (!modal || modal->Size.x < 200.0f || modal->Size.y < 60.0f) {
        printf("fail: confirmation modal is too small (%s, %.0fx%.0f)\n",
               modal ? "found" : "missing", modal ? modal->Size.x : 0.0f, modal ? modal->Size.y : 0.0f);
        return 1;
    }

    float clickX = modal->Pos.x + ImGui::GetStyle().WindowPadding.x + 5.0f;
    float clickY = modal->Pos.y + modal->Size.y - ImGui::GetStyle().WindowPadding.y - 12.0f;
    io.AddMousePosEvent(clickX, clickY);
    io.AddMouseButtonEvent(0, true);
    ImGui::NewFrame();
    ImGui::Begin("modal_probe");
    ConfirmationModal("probe_confirm", "Удалить правило", "Тестовое сообщение");
    ImGui::End();
    ImGui::Render();
    io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame();
    ImGui::Begin("modal_probe");
    confirmed = ConfirmationModal("probe_confirm", "Удалить правило", "Тестовое сообщение");
    ImGui::End();
    ImGui::Render();
    if (!confirmed) {
        printf("fail: clicking the confirm button did not register\n");
        return 1;
    }
    ImGui::NewFrame();
    ImGui::Begin("modal_probe");
    confirmed = ConfirmationModal("probe_confirm", "Удалить правило", "Тестовое сообщение");
    ImGui::End();
    ImGui::Render();
    if (confirmed) {
        printf("fail: confirmation modal stayed open after confirming\n");
        return 1;
    }
    printf("ok: destructive actions need a confirmed modal\n");

    // 7) палитра цветов в форме категорий
    if (NormalizeHexColor("ABC") != "#aabbcc" || NormalizeHexColor("#12345") != "" ||
        NormalizeHexColor("zzzzzz") != "") {
        printf("fail: NormalizeHexColor is wrong\n");
        return 1;
    }
    ImGuiWindow* palette = nullptr;
    for (int i = 0; i < 3; ++i) {
        ImGui::NewFrame();
        ImGui::Begin("palette_probe");
        std::string pickedColor = "#e74c3c";
        ImGui::OpenPopup("category_color_palette");
        ColorPalettePicker("category_color_palette", pickedColor);
        ImGui::End();
        ImGui::Render();
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
            if (strstr(w->Name, "##Popup") != nullptr) palette = w;
        }
    }
    if (!palette || palette->Size.x < 400.0f || palette->Size.y < 100.0f) {
        printf("fail: color palette popup is empty (%s, %.0fx%.0f)\n",
               palette ? "found" : "missing",
               palette ? palette->Size.x : 0.0f, palette ? palette->Size.y : 0.0f);
        return 1;
    }
    printf("ok: color palette popup renders (%.0fx%.0f)\n", palette->Size.x, palette->Size.y);

    rulesSummary->OpenTab(1);
    Frame(app);
    printf("ok: rules summary view switched to queue tab\n");

    // 8) изменение правил автоматически переприменяет отметки в Форме МЗ
    Run probe;
    probe.file = "probe.ods";
    probe.format = "ods";
    probe.organization = "Тестовая больница";
    probe.period = "2026";
    int64_t probeRun = app.Db().InsertRun(probe);
    int64_t probeSub = app.Db().InsertSubaccount(probeRun, Subaccount{"105.31", false, 0, 0, 0, 0});
    Item probeItem;
    probeItem.rowNo = 1;
    probeItem.nameRaw = "Шприц 5 мл";
    probeItem.nameNorm = "шприц 5 мл";
    probeItem.subaccount = "105.31";
    probeItem.closeDebit = 100.0;
    int64_t probeItemId = app.Db().InsertItem(probeRun, probeSub, probeItem);

    FormView* formView = app.CreateView<FormView>("Форма МЗ проба");
    formView->RefreshData();
    Frame(app);
    if (app.RulesOutOfDate(probeRun)) {
        printf("fail: freshly imported run is marked out of date\n");
        return 1;
    }

    Rule probeRule;
    probeRule.code = "PROBE01";
    probeRule.categoryId = app.Db().ListCategories().front().id;
    probeRule.type = "regex";
    probeRule.pattern = "sub:105.31; шприц";
    probeRule.severity = 2;
    if (!app.Db().AddRule(probeRule)) {
        printf("fail: cannot add the probe rule\n");
        return 1;
    }
    app.MarkRulesChanged();
    if (!app.RulesOutOfDate(probeRun)) {
        printf("fail: rule change did not mark the run out of date\n");
        return 1;
    }

    Frame(app);
    if (app.RulesOutOfDate(probeRun)) {
        printf("fail: the form did not re-apply the changed rules\n");
        return 1;
    }
    bool probeFlagged = false;
    for (const Flag& flag : app.Db().ListItemFlags(probeItemId)) {
        if (flag.ruleCode == "PROBE01") probeFlagged = true;
    }
    if (!probeFlagged) {
        printf("fail: re-applied rules did not flag the probe item\n");
        return 1;
    }
    printf("ok: rule changes re-apply flags automatically in the form\n");

    // 8а) фильтр строк по правилу в Форме МЗ
    Item secondItem;
    secondItem.rowNo = 2;
    secondItem.nameRaw = "Бинт стерильный";
    secondItem.nameNorm = "бинт стерильный";
    secondItem.subaccount = "105.31";
    secondItem.closeDebit = 50.0;
    secondItem.qtyClose = 2.0;
    app.Db().InsertItem(probeRun, probeSub, secondItem);

    formView->RefreshData();
    formView->SetRuleFilter("PROBE01");
    Frame(app);
    if (formView->ShownRows() != 1) {
        printf("fail: rule filter kept %d rows instead of 1\n", formView->ShownRows());
        return 1;
    }
    formView->SetRuleFilter("S02");
    Frame(app);
    if (formView->ShownRows() != 1) {
        printf("fail: rule S02 filter kept %d rows instead of 1\n", formView->ShownRows());
        return 1;
    }
    formView->SelectRules({"PROBE01", "S02"});
    Frame(app);
    if (formView->ShownRows() != 1) {
        printf("fail: multi-rule filter kept %d rows instead of 1\n", formView->ShownRows());
        return 1;
    }
    formView->SelectRules({});
    Frame(app);
    if (formView->ShownRows() != 2) {
        printf("fail: clearing the rule filter left %d rows instead of 2\n", formView->ShownRows());
        return 1;
    }
    formView->SelectRules({"PROBE99"});
    Frame(app);
    if (formView->ShownRows() != 0) {
        printf("fail: unknown rule still matched rows in the form\n");
        return 1;
    }
    formView->SelectRules({});
    printf("ok: the form filters rows by rule\n");

    RunsView* runs = app.CreateView<RunsView>("Реестр прогонов");
    runs->RefreshData();
    for (int i = 0; i < 2; ++i) Frame(app);
    if (!HasView(app, "Реестр прогонов")) {
        printf("fail: runs view not open\n");
        return 1;
    }
    printf("ok: runs view renders\n");

    // 9) отчёт: раскраска по категориям и фильтр строк по правилам (включая экспорт)
    ReportView* report = app.CreateView<ReportView>("Отчёт проба");
    report->RefreshData();
    if (report->FilteredRowCount() == 0) {
        printf("fail: report filter dropped the flagged probe item\n");
        return 1;
    }
    report->SelectRules({"PROBE01"});
    if (report->FilteredRowCount() != 1) {
        printf("fail: rule filter kept %zu rows instead of 1\n", report->FilteredRowCount());
        return 1;
    }
    report->SelectRules({"PROBE99"});
    if (report->FilteredRowCount() != 0) {
        printf("fail: unknown rule still matched rows\n");
        return 1;
    }
    report->SelectRules({});
    std::string csv = report->ExportCsv();
    std::string html = report->ExportHtml();
    if (csv.empty() || html.empty()) {
        printf("fail: report export failed\n");
        return 1;
    }
    auto readAll = [](const std::string& path) {
        std::ifstream in(path);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    std::string csvText = readAll(csv);
    std::string htmlText = readAll(html);
    if (csvText.find("PROBE01") == std::string::npos || csvText.find("Шприц") == std::string::npos) {
        printf("fail: CSV export does not contain the filtered row\n");
        return 1;
    }
    if (htmlText.find("PROBE01") == std::string::npos || htmlText.find("background:#") == std::string::npos) {
        printf("fail: HTML export is not colored by category\n");
        return 1;
    }
    printf("ok: report filters rows by rule and exports the filtered set\n");

    app.LogEvent("первое событие", LogInfo);
    app.LogEvent("второе событие", LogWarn);
    if (app.Events().size() < 2 || app.Events().back().text != "второе событие") {
        printf("fail: event journal did not record events\n");
        return 1;
    }
    app.LogEvent("второе событие", LogWarn);
    int duplicates = 0;
    for (const LogEntry& entry : app.Events()) {
        if (entry.text == "второе событие") ++duplicates;
    }
    if (duplicates != 1) {
        printf("fail: duplicate events were appended\n");
        return 1;
    }
    printf("ok: event journal records and deduplicates events\n");

    app.SetProgress("читаем справочник", 0.25);
    if (!app.HasProgress()) {
        printf("fail: progress state not set\n");
        return 1;
    }
    app.ClearProgress();
    if (app.HasProgress()) {
        printf("fail: progress state not cleared\n");
        return 1;
    }
    printf("ok: progress state is tracked\n");

    ImGuiFileDialog* dialog = ImGuiFileDialog::Instance();
    app.OpenFileDialog("RegistryFile", "справочник", ".xlsx");
    if (!dialog->IsOpened("RegistryFile")) {
        printf("fail: guarded open did not open the dialog\n");
        return 1;
    }
    app.OpenFileDialog("ImportFile", "оборот", ".ods");
    if (!dialog->IsOpened("ImportFile") || dialog->IsOpened("RegistryFile")) {
        printf("fail: a stale dialog blocked the new file dialog\n");
        return 1;
    }
    printf("ok: opening a second dialog replaces the stale one\n");

    dialog->OpenDialog("GhostDlg", "сирой диалог", ".db");
    Frame(app);
    app.CloseStaleDialogs({"NewDbDlg", "OpenDbDlg", "SaveDbAsDlg"});
    if (dialog->IsOpened("GhostDlg")) {
        printf("fail: watchdog did not close the orphaned dialog\n");
        return 1;
    }
    printf("ok: watchdog closes dialogs with no owning view\n");

    ImGui::DestroyContext();
    printf("test_views: OK\n");
    return 0;
}
