#include "../src/Database.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;

static void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    } else {
        std::cout << "ok: " << what << "\n";
    }
}

int main() {
    const std::string path = "/tmp/audit105_test.db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    Database db;
    if (!db.Open(path)) {
        std::cerr << "open failed\n";
        return 1;
    }

    Check(db.ListCategories().size() == 5, "seeded categories");
    Check(db.ListRules().size() == 18, "example rules seeded on new database");
    bool noTurnoverIsGray = false;
    for (const Rule& rule : db.ListRules()) {
        if (rule.code != "S05") continue;
        for (const Category& cat : db.ListCategories()) {
            if (cat.id == rule.categoryId && cat.color == "#9AA0A6") noTurnoverIsGray = true;
        }
    }
    Check(noTurnoverIsGray, "no-turnover rule uses the gray category");
    Check(db.CountRulesByCodePrefix("OMS") == 5, "OMS term rules seeded");
    Check(db.CountRulesByCodePrefix("SUB") == 2, "wrong-subaccount rules seeded");
    Check(db.CountRulesByCodePrefix("OS") == 3, "fixed-asset candidate rules seeded");
    Check(db.ListTerms().size() == 25, "example term lists seeded");

    int seededTermLists = 0;
    bool termListHasMeta = false;
    for (const TermList& list : db.ListTermLists()) {
        if (list.ruleCode.rfind("OMS", 0) != 0) continue;
        ++seededTermLists;
        if (!list.description.empty() && !list.sourceDoc.empty() && !list.asOfDate.empty()) {
            termListHasMeta = true;
        }
    }
    Check(seededTermLists == 5, "example term rules have their term lists");
    Check(termListHasMeta, "example term lists carry description, source and date");

    bool everyTermRuleHasList = true;
    for (const Rule& rule : db.ListRules()) {
        if (rule.type != "term") continue;
        bool hasList = false;
        for (const TermList& list : db.ListTermLists()) {
            if (list.ruleId == rule.id) {
                hasList = true;
                break;
            }
        }
        if (!hasList) everyTermRuleHasList = false;
    }
    Check(everyTermRuleHasList, "every term rule has a term list");

    bool hasOmsTerms = false;
    for (const Term& term : db.ListTerms()) {
        if (term.ruleCode == "OMS01") hasOmsTerms = true;
    }
    Check(hasOmsTerms, "OMS01 rule has its term list");

    auto categoryIdByName = [&](const std::string& name) -> int64_t {
        for (const Category& cat : db.ListCategories()) {
            if (cat.name == name) return cat.id;
        }
        return 0;
    };
    auto ruleIdByCode = [&](const std::string& code) -> int64_t {
        for (const Rule& rule : db.ListRules()) {
            if (rule.code == code) return rule.id;
        }
        return 0;
    };
    int64_t omsRuleId = ruleIdByCode("OMS01");
    int64_t attentionCategoryId = categoryIdByName("Обратить внимание");
    Check(omsRuleId > 0, "OMS01 rule resolved by id");
    Check(attentionCategoryId > 0, "category resolved by id");

    Run r;
    r.file = "105. 2026.ods";
    r.format = "ods";
    r.organization = "ГБ №1";
    r.period = "2026";
    r.note = "проверка";
    int64_t id = db.InsertRun(r);
    Check(id > 0, "insert run");

    Run r2;
    r2.file = "105. 2025.ods";
    r2.format = "ods";
    r2.organization = "ГБ №2";
    r2.period = "2025";
    int64_t id2 = db.InsertRun(r2);

    Check(db.UpdateRunStats(id, 1639, 120, 5), "update run stats");

    Run got;
    Check(db.GetRun(id, got) && got.items == 1639, "get run with stats");

    Check(db.ListRuns("", "", "", false).size() == 2, "list runs");
    Check(db.ListRuns("ГБ №1", "", "", false).size() == 1, "filter by organization");
    Check(db.ListRuns("", "2025", "", false).size() == 1, "filter by period");

    int64_t subId = db.InsertSubaccount(id, Subaccount{"105.31", false, 0, 0, 0, 0});
    Item item;
    item.rowNo = 10;
    item.nameRaw = "Перчатки хирургические";
    item.nameNorm = "перчатки хирургические";
    item.subaccount = "105.31";
    item.closeDebit = 1000.0;
    item.hasQtyRow = true;
    int64_t itemId = db.InsertItem(id, subId, item);
    Check(itemId > 0, "insert item");

    Note note;
    note.itemId = itemId;
    note.docNo = "Н-123";
    note.docDate = "12.01.2026";
    note.contract = "Д-45";
    note.note = "проверить источник финансирования";
    note.author = "аудитор";
    Check(db.AddNote(note), "add note");
    Check(db.ListNotes(itemId).size() == 1, "list notes");
    Note stored = db.ListNotes(itemId).front();
    Check(stored.docNo == "Н-123" && !stored.createdAt.empty(), "note fields persisted");
    stored.note = "уточнено";
    Check(db.UpdateNote(stored), "update note");
    Check(db.ListNotes(itemId).front().note == "уточнено", "note updated");
    Check(db.CountRunNotes(id) == 1, "run notes counted");
    Check(db.DeleteNote(stored.id), "delete note");
    Check(db.CountRunNotes(id) == 0, "notes removed with item");

    Rule rule;
    rule.code = "TEST_RULE";
    rule.categoryId = attentionCategoryId;
    rule.type = "term";
    rule.pattern = "sub:105.31;перчатки";
    rule.severity = 2;
    rule.rationale = "тестовое правило";
    Check(db.AddRule(rule), "add rule");
    Check(db.ListRules().size() == 19, "rule listed");
    int64_t ruleId = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.code == "TEST_RULE") ruleId = stored.id;
    }
    Check(ruleId > 0, "rule has id");
    rule.severity = 3;
    rule.id = ruleId;
    Check(db.UpdateRule(rule), "update rule");
    Check(db.ListRules().size() == 19, "rule count unchanged after update");
    Check(db.DeleteRule(ruleId), "delete rule");
    Check(db.ListRules().size() == 18, "rule removed");

    size_t termBase = db.ListTerms().size();
    Term term;
    term.ruleId = omsRuleId;
    term.term = "Иммуноглобулин";
    term.scope = "local";
    term.listName = "Перечень ОМС";
    Check(db.AddTerm(term), "add term");
    Check(db.ListTerms().size() == termBase + 1, "term listed");
    int64_t termId = 0;
    for (const Term& stored : db.ListTerms()) {
        if (stored.termNorm == "иммуноглобулин" && stored.scope == "local") termId = stored.id;
    }
    Check(termId > 0, "term normalized on insert");
    Term updated = term;
    updated.id = termId;
    updated.term = "Иммуноглобулин человеческий";
    updated.sourceDoc = "Приказ 123";
    updated.asOfDate = "2026-01-01";
    Check(db.UpdateTerm(updated), "update term");
    for (const Term& stored : db.ListTerms()) {
        if (stored.id == termId) {
            Check(stored.termNorm == "иммуноглобулин человеческий", "term updated normalized");
            Check(stored.sourceDoc == "Приказ 123", "term source doc saved");
        }
    }
    Check(db.DeleteTerm(termId), "delete term");
    Check(db.ListTerms().size() == termBase, "term removed");

    Term autoListTerm;
    autoListTerm.ruleId = omsRuleId;
    autoListTerm.term = "Термин с новым перечнем";
    autoListTerm.listName = "Перечень из термина";
    Check(db.AddTerm(autoListTerm), "term added with a brand new list name");
    int64_t autoListId = 0;
    for (const TermList& list : db.ListTermLists()) {
        if (list.ruleId == omsRuleId && list.name == "Перечень из термина") autoListId = list.id;
    }
    Check(autoListId != 0, "term list auto-created when a term names a new list");
    Check(db.DeleteTermList(autoListId), "auto-created term list removed");

    int64_t blankRuleId = db.CreateBlankRule(attentionCategoryId);
    Check(blankRuleId > 0, "blank rule created with id");
    Rule blank;
    for (const Rule& stored : db.ListRules()) {
        if (stored.id == blankRuleId) blank = stored;
    }
    Check(blank.code == "новый-" + std::to_string(blankRuleId), "blank rule gets provisional code");
    Check(blank.type == "term" && blank.active, "blank rule defaults");
    blank.code = "TEST_BLANK";
    blank.id = blankRuleId;
    Check(db.UpdateRule(blank), "blank rule saved after edit");
    Check(db.DeleteRule(blankRuleId), "blank rule removed");

    int64_t omsId = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.code == "OMS01") omsId = stored.id;
    }
    size_t rulesBeforeCopy = db.ListRules().size();
    int64_t copyId = db.DuplicateRule(omsId);
    Check(copyId > 0, "rule duplicated by copy");
    Rule copy;
    Check(db.GetRule(copyId, copy), "duplicate rule readable");
    Check(copy.code == "OMS01-копия", "duplicate code gets -копия suffix");
    Check(copy.category == "Недопустимо по ОМС" && copy.type == "term", "duplicate repeats category and type");
    Rule source;
    Check(db.GetRule(omsId, source), "source rule readable");
    Check(copy.severity == source.severity, "duplicate repeats severity");
    Check(copy.pattern == source.pattern && copy.rationale == source.rationale &&
          copy.sourceDoc == source.sourceDoc && copy.active == source.active,
          "duplicate repeats pattern, rationale, source and active flag");
    int64_t copyId2 = db.DuplicateRule(copyId);
    Rule copy2;
    Check(db.GetRule(copyId2, copy2) && copy2.code == "OMS01-копия-2", "second copy gets a unique code");
    Check(db.ListRules().size() == rulesBeforeCopy + 2, "copies are counted");
    Check(!db.DuplicateRule(0), "duplicate of missing rule fails");
    Check(db.DeleteRule(copyId) && db.DeleteRule(copyId2), "copies removed");

    Rule clash;
    clash.code = "новый-19";
    clash.categoryId = attentionCategoryId;
    clash.type = "structural";
    clash.pattern = "arith";
    Check(db.AddRule(clash), "rule with blank-like code added");
    int64_t clashId = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.code == clash.code) clashId = stored.id;
    }
    int64_t blankAgain = db.CreateBlankRule(0);
    Rule blankAgainRule;
    Check(db.GetRule(blankAgain, blankAgainRule), "blank rule created with fallback category");
    Check(blankAgainRule.code != "новый-19" && !blankAgainRule.code.empty(), "blank rule code placeholder is unique");
    Check(blankAgainRule.categoryId != 0 && !blankAgainRule.category.empty(), "blank rule category fallback filled");
    Check(db.DeleteRule(blankAgain) && db.DeleteRule(clashId), "placeholder rules removed");

    int64_t orphanTermId = db.CreateBlankTerm(0, "");
    Check(orphanTermId > 0, "blank term created without a rule");
    Check(db.DeleteTerm(orphanTermId), "orphan blank term removed");

    int64_t blankTermId = db.CreateBlankTerm(omsRuleId, "Перечень ОМС");
    Check(blankTermId > 0, "blank term created with id");
    int added = db.AddTermsToList(omsRuleId, "Перечень ОМС",
                                  {"Тест-термин А", "Тест-термин А", "  Тест-термин Б  ", ""},
                                  "Приказ 1", "2026-01-01");
    Check(added == 2, "bulk list add skips duplicates and blanks");
    bool hasList = false;
    for (const TermList& list : db.ListTermLists()) {
        if (list.ruleId == omsRuleId && list.name == "Перечень ОМС" && list.termCount >= 3) hasList = true;
    }
    Check(hasList, "term lists grouped by rule and list name");
    Check(db.CountRuleTerms(omsRuleId) > 0, "rule term count");
    Check(db.DeleteTerm(blankTermId), "blank term removed");

    int64_t sourceTermId = 0;
    for (const Term& stored : db.ListTerms()) {
        if (stored.ruleCode == "OMS01" && stored.listName == "Перечень ОМС") {
            sourceTermId = stored.id;
            break;
        }
    }
    size_t termsBeforeCopy = db.ListTerms().size();
    int64_t termCopyId = db.DuplicateTerm(sourceTermId);
    Check(termCopyId > 0, "term duplicated by copy");
    Term termCopy;
    Check(db.GetTerm(termCopyId, termCopy), "duplicate term readable");
    Check(termCopy.term.find("(копия)") != std::string::npos, "duplicate term name gets (копия) suffix");
    Check(termCopy.ruleCode == "OMS01" && termCopy.listName == "Перечень ОМС" &&
          termCopy.scope == "local" && termCopy.active, "duplicate term repeats rule, list and scope");
    int64_t termCopyId2 = db.DuplicateTerm(termCopyId);
    Term termCopy2;
    Check(db.GetTerm(termCopyId2, termCopy2) && termCopy2.term != termCopy.term,
          "second term copy gets a unique name");
    Check(db.ListTerms().size() == termsBeforeCopy + 2, "term copies are counted");
    Check(!db.DuplicateTerm(0), "duplicate of missing term fails");
    Check(db.DeleteTerm(termCopyId) && db.DeleteTerm(termCopyId2), "term copies removed");

    int listCopied = 0;
    std::string copiedListName = db.DuplicateTermList(omsRuleId, "Перечень ОМС", listCopied);
    Check(copiedListName == "Перечень ОМС (копия)", "list copy gets (копия) name");
    Check(listCopied > 0, "list copy duplicates terms");
    bool hasCopiedList = false;
    for (const TermList& list : db.ListTermLists()) {
        if (list.ruleCode == "OMS01" && list.name == copiedListName && list.termCount == listCopied) {
            hasCopiedList = true;
        }
    }
    Check(hasCopiedList, "copied list appears in term lists");
    std::string secondListName = db.DuplicateTermList(omsRuleId, "Перечень ОМС", listCopied);
    Check(secondListName == "Перечень ОМС (копия) 2", "second list copy gets a unique name");
    for (const Term& stored : db.ListTerms()) {
        if (stored.ruleCode == "OMS01" &&
            (stored.listName == copiedListName || stored.listName == secondListName)) {
            db.DeleteTerm(stored.id);
        }
    }
    Check(db.ListTerms().size() == termsBeforeCopy, "copied list terms removed");

    int64_t categoryId = 0;
    for (const Category& stored : db.ListCategories()) {
        if (stored.name == "Недопустимо по ОМС") categoryId = stored.id;
    }
    size_t categoriesBeforeCopy = db.ListCategories().size();
    int64_t categoryCopyId = db.DuplicateCategory(categoryId);
    Check(categoryCopyId > 0, "category duplicated by copy");
    Category categoryCopy;
    Check(db.GetCategory(categoryCopyId, categoryCopy), "duplicate category readable");
    Check(categoryCopy.name == "Недопустимо по ОМС (копия)", "duplicate category name gets (копия) suffix");
    Category sourceCategory;
    Check(db.GetCategory(categoryId, sourceCategory), "source category readable");
    Check(categoryCopy.color == sourceCategory.color && categoryCopy.active == sourceCategory.active,
          "duplicate category repeats color and active flag");
    int64_t categoryCopyId2 = db.DuplicateCategory(categoryCopyId);
    Category categoryCopy2;
    Check(db.GetCategory(categoryCopyId2, categoryCopy2) &&
          categoryCopy2.name == "Недопустимо по ОМС (копия) 2",
          "second category copy gets a unique name");
    Check(db.ListCategories().size() == categoriesBeforeCopy + 2, "category copies are counted");
    Check(!db.DuplicateCategory(0), "duplicate of missing category fails");
    Check(db.DeleteCategory(categoryCopyId) && db.DeleteCategory(categoryCopyId2), "category copies removed");

    Category tempCategory;
    tempCategory.name = "Тест-категория";
    tempCategory.color = "#3498db";
    tempCategory.sortOrder = 500;
    tempCategory.active = true;
    Check(db.AddCategory(tempCategory), "temporary category added");
    int64_t tempCategoryId = 0;
    for (const Category& stored : db.ListCategories()) {
        if (stored.name == tempCategory.name) tempCategoryId = stored.id;
    }
    Rule tempRule;
    tempRule.code = "TEST_CAT";
    tempRule.categoryId = tempCategoryId;
    tempRule.type = "structural";
    tempRule.pattern = "arith";
    Check(db.AddRule(tempRule), "rule added in the temporary category");
    int64_t tempRuleId = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.code == tempRule.code) tempRuleId = stored.id;
    }

    Category renamed = tempCategory;
    renamed.id = tempCategoryId;
    renamed.name = "Тест-категория переименована";
    Check(db.UpdateCategory(renamed), "category renamed while rules reference it");
    int movedRules = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.category == renamed.name) ++movedRules;
    }
    Check(movedRules == 1, "rules follow the renamed category");

    Category clashCat = renamed;
    clashCat.name = "Обратить внимание";
    Check(!db.UpdateCategory(clashCat), "renaming to an existing name fails");
    movedRules = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.category == renamed.name) ++movedRules;
    }
    Check(movedRules == 1, "failed rename did not move rules");

    Check(db.DeleteCategory(tempCategoryId), "category with rules deleted");
    int orphanRules = 0;
    for (const Rule& stored : db.ListRules()) {
        if (stored.category == renamed.name) ++orphanRules;
    }
    Check(orphanRules == 0, "rules moved away from the deleted category");
    Check(db.DeleteRule(tempRuleId), "temporary rule removed");

    int64_t blankCategoryId = db.CreateBlankCategory();
    Check(blankCategoryId > 0, "blank category created with id");
    bool foundBlank = false;
    for (const Category& stored : db.ListCategories()) {
        if (stored.id == blankCategoryId) foundBlank = true;
    }
    Check(foundBlank, "blank category listed");
    Check(db.DeleteCategory(blankCategoryId), "blank category removed");

    int64_t subId2 = db.InsertSubaccount(id2, Subaccount{"105.31", false, 0, 0, 0, 0});
    Item item2;
    item2.rowNo = 10;
    item2.nameRaw = "Перчатки хирургические";
    item2.nameNorm = "перчатки хирургические";
    item2.subaccount = "105.31";
    item2.closeDebit = 1500.0;
    int64_t itemId2 = db.InsertItem(id2, subId2, item2);
    Item item3;
    item3.rowNo = 20;
    item3.nameRaw = "Шприцы";
    item3.nameNorm = "шприцы";
    item3.subaccount = "105.31";
    item3.closeDebit = 800.0;
    db.InsertItem(id2, subId2, item3);
    int64_t s01RuleId = ruleIdByCode("S01");
    Check(s01RuleId > 0, "S01 rule resolved by id");
    db.InsertFlag(itemId, s01RuleId, 2, 1000.0, "без оборота");
    db.InsertFlag(itemId2, s01RuleId, 2, 1500.0, "без оборота");

    CompareResult cmp = db.CompareRuns(id, id2);
    Check(cmp.both == 1, "one common normalized name");
    Check(cmp.onlyB == 1, "one name only in B");
    Check(cmp.onlyA == 0, "no name only in A");
    Check(cmp.changed == 1, "changed balance detected");
    Check(cmp.flaggedCountA == 1 && cmp.flaggedCountB == 1, "flagged counted in both runs");
    Check(cmp.flaggedA == 1000.0 && cmp.flaggedB == 1500.0, "flagged amounts summed per run");
    Check(!cmp.rows.empty() && cmp.rows.front().nameNorm == "шприцы",
         "biggest delta sorted first");

    std::vector<SubaccountCompare> subs = db.CompareSubaccounts(id, id2);
    Check(subs.size() == 1, "one subaccount compared");
    Check(subs.front().code == "105.31", "subaccount code");
    Check(subs.front().balanceA == 1000.0 && subs.front().balanceB == 2300.0,
         "subaccount balances per run");
    Check(subs.front().flaggedA == 1000.0 && subs.front().flaggedB == 1500.0,
         "subaccount flagged amounts per run");
    Check(subs.front().itemsA == 1 && subs.front().itemsB == 2, "subaccount item counts");

    std::vector<RuleCompare> rules = db.CompareRules(id, id2);
    Check(rules.size() == 1, "one rule compared");
    Check(rules.front().ruleCode == "S01", "rule code");
    Check(rules.front().amountA == 1000.0 && rules.front().amountB == 1500.0,
         "rule flagged amounts per run");
    Check(rules.front().countA == 1 && rules.front().countB == 1, "rule flag counts");

    Rule pkgRule;
    pkgRule.code = "PKG_RULE";
    pkgRule.categoryId = attentionCategoryId;
    pkgRule.type = "regex";
    pkgRule.pattern = "кабинет";
    pkgRule.severity = 1;
    Check(db.AddRule(pkgRule), "add rule for package export");

    Run example;
    example.file = "logic.ods";
    example.format = "ods";
    example.organization = "ГБ №3";
    example.period = "2026";
    example.note = "проверка логики примеров";
    int64_t runEx = db.InsertRun(example);
    int64_t sub31 = db.InsertSubaccount(runEx, Subaccount{"105.31", false, 0, 0, 0, 0});
    int64_t sub34 = db.InsertSubaccount(runEx, Subaccount{"105.34", false, 0, 0, 0, 0});

    Item omsItem;
    omsItem.rowNo = 1;
    omsItem.nameRaw = "Иммуноглобулин человеческий 5 мл";
    omsItem.nameNorm = "иммуноглобулин человеческий 5 мл";
    omsItem.subaccount = "105.31";
    omsItem.closeDebit = 5000.0;
    int64_t omsItemId = db.InsertItem(runEx, sub31, omsItem);

    Item nonMed;
    nonMed.rowNo = 2;
    nonMed.nameRaw = "Автозапчасти для автомобиля";
    nonMed.nameNorm = "автозапчасти для автомобиля";
    nonMed.subaccount = "105.31";
    nonMed.closeDebit = 800.0;
    int64_t nonMedId = db.InsertItem(runEx, sub31, nonMed);

    Item fixedAsset;
    fixedAsset.rowNo = 3;
    fixedAsset.nameRaw = "Оборудование для рентгена";
    fixedAsset.nameNorm = "оборудование для рентгена";
    fixedAsset.subaccount = "105.34";
    fixedAsset.closeDebit = 60000.0;
    int64_t fixedAssetId = db.InsertItem(runEx, sub34, fixedAsset);

    int64_t sub36 = db.InsertSubaccount(runEx, Subaccount{"105.36", false, 0, 0, 0, 0});
    Item reagent;
    reagent.rowNo = 4;
    reagent.nameRaw = "Аланинаминотрансфераза (АЛТ) ИВД, набор реагентов";
    reagent.nameNorm = "аланинаминотрансфераза (алт) ивд, набор реагентов";
    reagent.subaccount = "105.36";
    reagent.closeDebit = 1200.0;
    int64_t reagentId = db.InsertItem(runEx, sub36, reagent);

    int flagged = db.ApplyRules(runEx);
    Check(flagged == 4, "example rules flag four positions");
    std::vector<Flag> omsFlags = db.ListItemFlags(omsItemId);
    std::vector<Flag> nonMedFlags = db.ListItemFlags(nonMedId);
    std::vector<Flag> assetFlags = db.ListItemFlags(fixedAssetId);
    std::vector<Flag> reagentFlags = db.ListItemFlags(reagentId);
    auto has = [](const std::vector<Flag>& flags, const std::string& code) {
        for (const Flag& flag : flags) {
            if (flag.ruleCode == code) return true;
        }
        return false;
    };
    Check(has(omsFlags, "OMS01"), "term rule flags immunoglobulin as not OMS");
    Check(has(nonMedFlags, "SUB01"), "synonym rule flags auto parts on 105.31");
    Check(has(reagentFlags, "SUB03"), "reagents and IVD sets on 105.36 flagged as wrong subaccount");
    Check(has(assetFlags, "OS01") && has(assetFlags, "OS02") && has(assetFlags, "OS03"),
         "equipment on 105.34 flagged as fixed-asset candidate");

    std::string pkg = db.ExportDictionaryPackage("/tmp/audit105_dict.json");
    Check(!pkg.empty(), "dictionary package exported");

    const std::string path2 = "/tmp/audit105_test2.db";
    std::remove(path2.c_str());
    std::remove((path2 + "-wal").c_str());
    std::remove((path2 + "-shm").c_str());
    Database db2;
    Check(db2.Open(path2), "open second db");
    DictImportStats stats;
    Check(db2.ImportDictionaryPackage(pkg, stats), "dictionary package imported");
    Check(stats.categoriesUpdated == 5 && stats.categoriesAdded == 0,
         "seeded categories updated, not duplicated");
    Check(stats.rulesAdded == 1 && stats.rulesUpdated == 18,
         "new rule added, existing rules updated");
    Check(db2.ListCategories().size() == 5 && db2.ListRules().size() == 19,
         "dictionaries present in second db");

    DictImportStats stats2;
    Check(db.ImportDictionaryPackage(pkg, stats2), "re-import into original db");
    Check(stats2.categoriesAdded == 0 && stats2.categoriesUpdated == 5,
         "categories updated, not duplicated");
    Check(stats2.rulesAdded == 0 && stats2.rulesUpdated == 19,
         "rules updated, not duplicated");

    int64_t s01InDb2 = 0;
    for (const Rule& rule : db2.ListRules()) {
        if (rule.code == "S01") s01InDb2 = rule.id;
    }
    Term manual;
    manual.ruleId = s01InDb2;
    manual.term = "Шприцы";
    manual.origin = "manual";
    Check(db2.AddTerm(manual), "add manual term to second db");
    {
        std::ofstream conflictFile("/tmp/audit105_conflict.json");
        conflictFile << "{\"version\":1,\"categories\":[],\"rules\":[],\"terms\":[{\"ruleCode\":\"S02\",\"term\":\"шприцы\",\"scope\":\"local\",\"origin\":\"import\"}]}";
    }
    DictImportStats stats3;
    Check(db2.ImportDictionaryPackage("/tmp/audit105_conflict.json", stats3), "conflict import");
    Check(stats3.conflicts == 1 && stats3.termsAdded == 0, "manual term not overwritten, conflict recorded");

    int64_t blankListId = db.CreateBlankList(0);
    Check(blankListId != 0, "blank term list created");
    TermList blankList;
    Check(db.GetTermList(blankListId, blankList), "blank term list readable");
    Check(!blankList.name.empty(), "blank term list gets a name");
    blankList.id = blankListId;
    blankList.name = "Справочный перечень";
    blankList.description = "Проверочный перечень без правила";
    blankList.sourceDoc = "Приказ 2";
    blankList.asOfDate = "2026-02-01";
    Check(db.UpdateTermList(blankList), "blank term list updated");
    bool blankVisible = false;
    for (const TermList& list : db.ListTermLists()) {
        if (list.id == blankListId && list.name == "Справочный перечень" &&
            list.sourceDoc == "Приказ 2" && list.asOfDate == "2026-02-01") {
            blankVisible = true;
        }
    }
    Check(blankVisible, "updated term list visible in term lists");

    int64_t duplicateListId = db.DuplicateTermListById(blankListId, listCopied);
    Check(duplicateListId != 0, "term list duplicated by id");
    TermList duplicateList;
    Check(db.GetTermList(duplicateListId, duplicateList), "duplicated term list readable");
    Check(duplicateList.name.find("копия") != std::string::npos, "duplicated term list gets (копия) name");
    Check(db.DeleteTermList(duplicateListId), "duplicated term list deleted");
    Check(!db.GetTermList(duplicateListId, duplicateList), "deleted term list is gone");
    Check(db.DeleteTermList(blankListId), "blank term list deleted");
    Check(db.ListTermLists().size() >= 1, "term lists survive after cleanup");

    const std::string legacyPath = "/tmp/audit105_legacy_lists.db";
    std::remove(legacyPath.c_str());
    std::remove((legacyPath + "-wal").c_str());
    std::remove((legacyPath + "-shm").c_str());
    {
        Database legacy;
        Check(legacy.Open(legacyPath), "legacy db opened");
        int64_t legacyRuleId = legacy.CreateBlankRule(0);
        Term legacyTerm;
        legacyTerm.ruleId = legacyRuleId;
        legacyTerm.term = "Термин из старого перечня";
        legacyTerm.listName = "Старый перечень";
        legacyTerm.sourceDoc = "Приказ 3";
        legacyTerm.asOfDate = "2025-05-05";
        Check(legacy.AddTerm(legacyTerm), "legacy term added");
        legacy.Close();
    }
    Check(system("sqlite3 /tmp/audit105_legacy_lists.db \"CREATE TABLE list_kinds(id INTEGER PRIMARY KEY, name TEXT); "
                 "INSERT INTO list_kinds(id, name) VALUES(1, 'Лекарства'); "
                 "ALTER TABLE terms ADD COLUMN list_kind_id INTEGER DEFAULT 1;\"") == 0,
          "legacy list_kinds fixture created");
    Database migrated;
    Check(migrated.Open(legacyPath), "legacy db reopened");
    Check(!migrated.TableExists("list_kinds"), "list_kinds table removed by migration");
    bool migratedList = false;
    for (const TermList& list : migrated.ListTermLists()) {
        if (list.name == "Старый перечень" && list.sourceDoc == "Приказ 3" &&
            list.asOfDate == "2025-05-05" && list.termCount == 1) {
            migratedList = true;
        }
    }
    Check(migratedList, "legacy term list migrated into term_lists with source and date");

    int64_t tmpRule = db.CreateBlankRule(categoryIdByName("Обратить внимание"));
    int tmpAdded = db.AddTermsToList(tmpRule, "Временный перечень",
                                     {"временный термин один", "временный термин два"});
    Check(tmpAdded == 2, "temporary rule got its term list");
    Run tmpRun;
    tmpRun.file = "tmp.ods";
    tmpRun.organization = "Тест";
    tmpRun.period = "2026";
    int64_t tmpRunId = db.InsertRun(tmpRun);
    int64_t tmpSub = db.InsertSubaccount(tmpRunId, Subaccount{"105.31", false, 0, 0, 0, 0});
    Item tmpItem;
    tmpItem.rowNo = 1;
    tmpItem.nameRaw = "Временный термин один";
    tmpItem.nameNorm = "временный термин один";
    tmpItem.subaccount = "105.31";
    tmpItem.closeDebit = 100.0;
    int64_t tmpItemId = db.InsertItem(tmpRunId, tmpSub, tmpItem);
    db.InsertFlag(tmpItemId, tmpRule, 1, 100.0, "временная отметка");
    Check(db.CountRuleFlags(tmpRule) == 1, "temporary rule has a flag");

    int termsGone = 0;
    int flagsGone = 0;
    int rulesGone = db.DeleteRules(std::vector<int64_t>{tmpRule}, termsGone, flagsGone);
    Check(rulesGone == 1, "bulk delete removed the selected rule");
    Check(termsGone == 2, "bulk delete removed the rule terms");
    Check(flagsGone == 1, "bulk delete removed the rule flags");
    Check(db.ListRules().size() == 19, "rule list back to seeded size after bulk delete");
    int tmpItems = 0;
    double tmpSum = 0.0;
    Check(db.DeleteRun(tmpRunId, tmpItems, tmpSum), "temporary run removed");

    std::string backup;
    Check(db.Backup("test", backup), "backup");

    std::cout << db.IntegrityReport() << "\n";

    int removed = 0;
    double sum = 0;
    Check(db.DeleteRun(id2, removed, sum), "delete run");
    Check(db.ListRuns("", "", "", false).size() == 2, "run removed");

    std::cout << (failures ? "test_db: FAILED\n" : "test_db: OK\n");
    return failures ? 1 : 0;
}
