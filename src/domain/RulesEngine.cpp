#include "RulesEngine.h"

#include "Utf8.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

const double kEpsilon = 0.01;

double ArithGap(const Item& item) {
    return std::fabs(item.openDebit - item.openCredit + item.inDebit - item.outCredit -
                     (item.closeDebit - item.closeCredit));
}

double ClosingBalance(const Item& item) {
    return std::fabs(item.closeDebit - item.closeCredit);
}

std::vector<std::string> SplitPattern(const std::string& pattern) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : pattern) {
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

bool SubaccountAllowed(const PatternParts& pattern, const Item& item) {
    if (pattern.subaccounts.empty()) return true;
    for (const std::string& code : pattern.subaccounts) {
        if (EqualsUtf8Insensitive(item.subaccount, code)) return true;
    }
    return false;
}

bool StructuralMatch(const Item& item, const std::string& pattern, double& amount) {
    std::string key = KeywordName(pattern);
    double threshold = KeywordThreshold(pattern);

    if (key == "arith") {
        if (item.arithOk) return false;
        amount = ArithGap(item);
        return true;
    }
    if (key == "qty_missing") {
        if (item.hasQtyRow) return false;
        amount = ClosingBalance(item);
        return true;
    }
    if (key == "qty_mismatch") {
        if (!item.qtyMismatch) return false;
        amount = ClosingBalance(item);
        return true;
    }
    if (key == "negative") {
        if (item.closeDebit - item.closeCredit >= 0.0) return false;
        amount = ClosingBalance(item);
        return true;
    }
    if (key == "no_turnover") {
        if (item.inDebit > 0.0 || item.outCredit > 0.0) return false;
        if (std::fabs(item.closeDebit - item.closeCredit) < kEpsilon) return false;
        if (threshold > 0.0 && ClosingBalance(item) < threshold) return false;
        amount = ClosingBalance(item);
        return true;
    }
    if (key == "zero_qty") {
        if (ClosingBalance(item) <= kEpsilon) return false;
        if (std::fabs(item.qtyClose) > kEpsilon) return false;
        amount = ClosingBalance(item);
        return true;
    }
    return false;
}

}  // namespace

PatternParts ParseRulePattern(const std::string& pattern) {
    PatternParts parsed;
    std::string text = TrimUtf8(pattern);
    if (text.rfind("sub:", 0) == 0) {
        size_t sep = text.find(';');
        std::string codes = (sep == std::string::npos) ? text.substr(4) : text.substr(4, sep - 4);
        parsed.body = (sep == std::string::npos) ? std::string() : TrimUtf8(text.substr(sep + 1));
        for (const std::string& code : SplitPattern(codes)) parsed.subaccounts.push_back(code);
    } else {
        parsed.body = text;
    }
    return parsed;
}

std::string BuildRulePattern(const PatternParts& parts) {
    std::string result;
    if (!parts.subaccounts.empty()) {
        result = "sub:";
        for (size_t i = 0; i < parts.subaccounts.size(); ++i) {
            if (i > 0) result += ",";
            result += parts.subaccounts[i];
        }
        if (!parts.body.empty()) result += "; ";
    }
    result += parts.body;
    return result;
}

std::vector<std::string> StructuralKeywords() {
    return {"arith", "qty_missing", "qty_mismatch", "negative", "no_turnover", "zero_qty"};
}

std::vector<std::string> ExactKeywords() {
    return {"signature", "name"};
}

std::vector<KeywordHelp> KeywordHelpFor(const std::string& type) {
    if (type == "structural") {
        return {
            {"arith",
             "Начало (дебет − кредит) + приход − расход не равно остатку на конец. Сумма отметки — величина расхождения.",
             "sub:105.31,105.32; arith", "arith"},
            {"qty_missing",
             "В позиции нет строки «Кол.». Отметка по остатку на конец периода.",
             "sub:105.31; qty_missing", "qty_missing"},
            {"qty_mismatch",
             "Сумма и количество заполнены несогласованно: количество не соответствует остатку.",
             "sub:105.31; qty_mismatch", "qty_mismatch"},
            {"negative",
             "Остаток на конец отрицательный (кредит больше дебета).",
             "sub:105.31; negative", "negative"},
            {"no_turnover[:порог]",
             "Нет ни прихода, ни расхода, но остаток не нулевой. С порогом — только если остаток не меньше порога.",
             "sub:105.36; no_turnover:10000", "no_turnover"},
            {"zero_qty",
             "Сумма есть, а количество нулевое.",
             "sub:105.31; zero_qty", "zero_qty"},
        };
    }
    if (type == "exact") {
        return {
            {"signature",
             "Совпадают нормализованное наименование, субсчёт и все шесть сумм (начало, приход, расход, конец).",
             "sub:105.31; signature", "signature"},
            {"name",
             "Совпадают только нормализованное наименование и субсчёт.",
             "sub:105.31; name", "name"},
        };
    }
    if (type == "synonym") {
        return {
            {"список слов через запятую",
             "Слово совпадает, если оно встречается в нормализованном названии позиции (регистр и лишние пробелы игнорируются).",
             "sub:105.31; шприц, игла, инъекция", ""},
        };
    }
    if (type == "term") {
        return {
            {"термины из справочника",
             "Правило берёт активные термины из «Термины и списки» по коду правила. Тело шаблона — запасной список, если терминов нет.",
             "sub:105.31; вакцин", ""},
        };
    }
    return {
        {"регулярное выражение",
         "Поиск по нормализованному названию позиции (ECMAScript). Неверное выражение — правило молча пропускается.",
         "sub:105.31; ^вакцин|иммуноглобулин", ""},
    };
}

std::string KeywordName(const std::string& body) {
    std::string text = TrimUtf8(body);
    size_t colon = text.find(':');
    if (colon != std::string::npos) text = TrimUtf8(text.substr(0, colon));
    return ToLowerUtf8(text);
}

double KeywordThreshold(const std::string& body) {
    size_t colon = body.find(':');
    if (colon == std::string::npos) return 0.0;
    std::string tail = TrimUtf8(body.substr(colon + 1));
    if (tail.empty()) return 0.0;
    bool ok = false;
    double value = ParseNumber(tail, ok);
    return ok ? value : 0.0;
}

std::string ExactSignature(const Item& item) {
    std::ostringstream signature;
    signature << item.nameNorm << '|' << item.subaccount << '#'
              << std::fixed << std::setprecision(2)
              << item.openDebit << ',' << item.openCredit << ','
              << item.inDebit << ',' << item.outCredit << ','
              << item.closeDebit << ',' << item.closeCredit;
    return signature.str();
}

std::string NameSignature(const Item& item) {
    return item.nameNorm + "|" + item.subaccount;
}

std::vector<std::vector<FlagResult>> EvaluateRules(const std::vector<Item>& items,
                                                   const std::vector<Rule>& rules,
                                                   const std::vector<Term>& terms) {
    std::vector<std::vector<FlagResult>> result(items.size());

    std::vector<Rule> active;
    active.reserve(rules.size());
    for (const Rule& rule : rules) {
        if (rule.active && !rule.code.empty()) active.push_back(rule);
    }

    auto apply = [&](const Rule& rule, size_t index, double amount) {
        FlagResult flag;
        flag.ruleId = rule.id;
        flag.ruleCode = rule.code;
        flag.severity = rule.severity;
        flag.amount = amount;
        flag.rationale = rule.rationale;
        result[index].push_back(std::move(flag));
    };

    // 1. structural
    for (const Rule& rule : active) {
        if (rule.type != "structural") continue;
        PatternParts parsed = ParseRulePattern(rule.pattern);
        for (size_t i = 0; i < items.size(); ++i) {
            if (!SubaccountAllowed(parsed, items[i])) continue;
            double amount = 0.0;
            if (StructuralMatch(items[i], parsed.body, amount)) apply(rule, i, amount);
        }
    }

    // 2. exact duplicates
    for (const Rule& rule : active) {
        if (rule.type != "exact") continue;
        PatternParts parsed = ParseRulePattern(rule.pattern);
        std::unordered_set<std::string> seen;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!SubaccountAllowed(parsed, items[i])) continue;
            const std::string& key = (parsed.body == "name") ? items[i].nameNorm
                                                            : ExactSignature(items[i]);
            if (seen.count(key)) apply(rule, i, ClosingBalance(items[i]));
            seen.insert(key);
        }
    }

    // 3. synonym groups (pattern = comma separated synonyms)
    for (const Rule& rule : active) {
        if (rule.type != "synonym") continue;
        PatternParts parsed = ParseRulePattern(rule.pattern);
        std::vector<std::string> synonyms;
        for (const std::string& part : SplitPattern(parsed.body)) {
            synonyms.push_back(ToLowerUtf8(part));
        }
        if (synonyms.empty()) continue;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!SubaccountAllowed(parsed, items[i])) continue;
            for (const std::string& synonym : synonyms) {
                if (ContainsUtf8(items[i].nameNorm, synonym)) {
                    apply(rule, i, ClosingBalance(items[i]));
                    break;
                }
            }
        }
    }

    // 4. term lists
    std::unordered_map<int64_t, std::vector<std::string>> termsByRule;
    for (const Term& term : terms) {
        if (!term.active || term.ruleId == 0) continue;
        std::string norm = term.termNorm.empty() ? ToLowerUtf8(term.term) : term.termNorm;
        if (!norm.empty()) termsByRule[term.ruleId].push_back(norm);
    }
    for (const Rule& rule : active) {
        if (rule.type != "term") continue;
        PatternParts parsed = ParseRulePattern(rule.pattern);
        auto it = termsByRule.find(rule.id);
        const std::vector<std::string>& list = (it != termsByRule.end())
                                                  ? it->second
                                                  : SplitPattern(parsed.body);
        if (list.empty()) continue;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!SubaccountAllowed(parsed, items[i])) continue;
            for (const std::string& needle : list) {
                if (ContainsUtf8(items[i].nameNorm, needle)) {
                    apply(rule, i, ClosingBalance(items[i]));
                    break;
                }
            }
        }
    }

    // 5. regex
    for (const Rule& rule : active) {
        if (rule.type != "regex") continue;
        PatternParts parsed = ParseRulePattern(rule.pattern);
        std::regex expression;
        try {
            expression = std::regex(parsed.body, std::regex::ECMAScript);
        } catch (const std::regex_error&) {
            continue;
        }
        for (size_t i = 0; i < items.size(); ++i) {
            if (!SubaccountAllowed(parsed, items[i])) continue;
            if (std::regex_search(items[i].nameNorm, expression)) {
                apply(rule, i, ClosingBalance(items[i]));
            }
        }
    }

    return result;
}
