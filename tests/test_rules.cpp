#include "../src/domain/RulesEngine.h"
#include "../src/domain/Utf8.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

static void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    } else {
        std::cout << "ok: " << what << "\n";
    }
}

static Item MakeItem(const std::string& name, const std::string& sub,
                     double open, double debit, double credit, double close,
                     double qtyClose = 1.0, bool hasQty = true) {
    Item item;
    item.nameRaw = name;
    item.nameNorm = ToLowerUtf8(TrimUtf8(name));
    item.subaccount = sub;
    item.openDebit = open;
    item.openCredit = 0.0;
    item.inDebit = debit;
    item.outCredit = credit;
    item.closeDebit = close;
    item.closeCredit = 0.0;
    item.qtyClose = qtyClose;
    item.hasQtyRow = hasQty;
    item.arithOk = std::fabs(open + debit - credit - close) < 0.01;
    return item;
}

int main() {
    std::vector<Rule> rules = {
        {1, 1, "S01", "Обратить внимание", "structural", "arith", 3, "арифметика", "", true},
        {2, 1, "S04", "Обратить внимание", "structural", "negative", 3, "отрицательный остаток", "", true},
        {3, 1, "S05", "Обратить внимание", "structural", "no_turnover:1000", 1, "без оборота", "", true},
        {4, 1, "S07", "Обратить внимание", "exact", "signature", 3, "точный дубликат", "", true},
        {5, 1, "S02", "Обратить внимание", "structural", "qty_missing", 2, "нет Кол.", "", true},
        {6, 2, "OMS01", "Недопустимо по ОМС", "term", "sub:105.31;", 3, "перечень ОМС", "", true},
        {7, 3, "SUB01", "Неверный субсчёт", "synonym", "sub:105.31;автозапчасти,гсм", 2, "не тот субсчёт", "", true},
        {9, 3, "SUB03", "Неверный субсчёт", "synonym", "sub:105.36;реагент,ивд,набор", 2, "не тот субсчёт", "", true},
        {8, 1, "RX01", "Обратить внимание", "regex", "шприц", 1, "regex-проверка", "", true},
    };

    std::vector<Term> terms = {
        {1, 6, "OMS01", "Иммуноглобулин", "иммуноглобулин", "local", "manual", "Перечень ОМС", "", "", true},
    };

    std::vector<Item> items;
    items.push_back(MakeItem("Левофлоксацин тб", "105.31", 1000, 0, 0, 1000, 5));
    items.push_back(MakeItem("Иммуноглобулин", "105.31", 5000, 0, 0, 5000, 2));
    items.push_back(MakeItem("Автозапчасти", "105.31", 800, 0, 0, 800, 1));
    items.push_back(MakeItem("Шприц", "105.36", 300, 100, 50, 350, 10));
    items.push_back(MakeItem("Перчатки", "105.36", 500, 0, 0, 500, 0, false));
    items.push_back(MakeItem("Перчатки", "105.36", 500, 0, 0, 500, 0, false));
    items.push_back(MakeItem("Бинт", "105.36", 400, 0, 0, -100, 1));
    items.push_back(MakeItem("Соль", "105.36", 100, 0, 0, 120, 1));
    items.push_back(MakeItem("Катетер", "105.31", 300, 0, 0, 300, 1));
    items.push_back(MakeItem("Реагент ИВД, набор", "105.36", 400, 0, 0, 400, 1));

    std::vector<std::vector<FlagResult>> flags = EvaluateRules(items, rules, terms);

    auto hasFlag = [&](size_t index, const std::string& code) {
        for (const FlagResult& flag : flags[index]) {
            if (flag.ruleCode == code) return true;
        }
        return false;
    };

    Check(hasFlag(1, "S05"), "no_turnover above threshold flagged");
    Check(hasFlag(1, "OMS01"), "term rule flags immunoglobulin");
    Check(hasFlag(2, "SUB01"), "synonym rule flags auto parts on 105.31");
    Check(!hasFlag(2, "S05"), "stock below no_turnover threshold is not flagged");
    Check(hasFlag(3, "S05") == false, "item with turnover is not flagged as no_turnover");
    Check(hasFlag(4, "S02"), "missing qty row flagged");
    Check(hasFlag(5, "S07"), "exact duplicate flagged");
    Check(hasFlag(6, "S04"), "negative balance flagged");
    Check(hasFlag(7, "S01"), "arithmetic mismatch flagged");
    Check(hasFlag(3, "RX01"), "regex rule flags syringe");
    Check(!hasFlag(8, "SUB01") && !hasFlag(8, "SUB03"), "medical supplies on 105.31 are not flagged as wrong subaccount");
    Check(hasFlag(9, "SUB03"), "reagents and IVD sets on 105.36 flagged as wrong subaccount");

    std::vector<Rule> termOnly = {
        {20, 2, "T01", "Недопустимо по ОМС", "term", "sub:105.31;шприц", 3, "перечень", "", true},
    };
    std::vector<Item> termItems = {MakeItem("Шприц", "105.31", 100, 0, 0, 100, 1)};
    std::vector<std::vector<FlagResult>> withoutTerms = EvaluateRules(termItems, termOnly, {});
    Check(withoutTerms[0].empty(), "term rule without terms does not fall back to pattern body");
    std::vector<Term> termList = {{30, 20, "T01", "Шприц", "шприц", "local", "manual", "Список", "", "", true}};
    std::vector<std::vector<FlagResult>> withTerms = EvaluateRules(termItems, termOnly, termList);
    Check(!withTerms[0].empty() && withTerms[0][0].ruleCode == "T01", "term rule flags from its list");
    std::vector<Term> inactiveTerms = {{30, 20, "T01", "Шприц", "шприц", "local", "manual", "Список", "", "", 0, false}};
    std::vector<std::vector<FlagResult>> inactive = EvaluateRules(termItems, termOnly, inactiveTerms);
    Check(inactive[0].empty(), "inactive terms do not flag");

    std::vector<Item> big = {MakeItem("Замороженный запас", "105.36", 20000, 0, 0, 20000, 3)};
    std::vector<std::vector<FlagResult>> bigFlags = EvaluateRules(big, rules, terms);
    Check(!bigFlags[0].empty() && bigFlags[0][0].ruleCode == "S05", "threshold lets large frozen stock through");

    for (const std::string& type : {"structural", "exact", "synonym", "term", "regex"}) {
        std::vector<KeywordHelp> help = KeywordHelpFor(type);
        Check(!help.empty(), "keyword help exists for type " + type);
        for (const KeywordHelp& info : help) {
            Check(!info.logic.empty() && !info.example.empty(),
                  "help entry for type " + type + " has logic and example");
        }
    }
    for (const std::string& keyword : StructuralKeywords()) {
        bool documented = false;
        for (const KeywordHelp& info : KeywordHelpFor("structural")) {
            if (info.keyword == keyword) documented = true;
        }
        Check(documented, "structural keyword " + keyword + " is documented in the help");
    }
    for (const std::string& keyword : ExactKeywords()) {
        bool documented = false;
        for (const KeywordHelp& info : KeywordHelpFor("exact")) {
            if (info.keyword == keyword) documented = true;
        }
        Check(documented, "exact keyword " + keyword + " is documented in the help");
    }
    Check(KeywordHelpFor("synonym").front().keyword.empty(), "synonym body is free text in the help");

    PatternParts parsed = ParseRulePattern("sub:105.31, 105.32; no_turnover:10000");
    Check(parsed.subaccounts.size() == 2 && KeywordName(parsed.body) == "no_turnover",
          "pattern parts parsed for the help panel");
    Check(BuildRulePattern(parsed) == "sub:105.31,105.32; no_turnover:10000",
          "pattern round-trips through the editor");

    std::cout << (failures ? "test_rules: FAILED\n" : "test_rules: OK\n");
    return failures ? 1 : 0;
}
