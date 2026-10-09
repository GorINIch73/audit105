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

struct RulePattern {
    std::vector<std::string> subaccounts;
    std::string body;
};

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

RulePattern ParsePattern(const std::string& pattern) {
    RulePattern parsed;
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

bool SubaccountAllowed(const RulePattern& pattern, const Item& item) {
    if (pattern.subaccounts.empty()) return true;
    for (const std::string& code : pattern.subaccounts) {
        if (EqualsUtf8Insensitive(item.subaccount, code)) return true;
    }
    return false;
}

bool StructuralMatch(const Item& item, const std::string& pattern, double& amount) {
    std::string key = pattern;
    double threshold = 0.0;
    size_t colon = key.find(':');
    if (colon != std::string::npos) {
        std::string tail = TrimUtf8(key.substr(colon + 1));
        key = TrimUtf8(key.substr(0, colon));
        if (!tail.empty()) {
            bool ok = false;
            threshold = ParseNumber(tail, ok);
            if (!ok) threshold = 0.0;
        }
    }

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
        flag.ruleCode = rule.code;
        flag.category = rule.category;
        flag.severity = rule.severity;
        flag.amount = amount;
        flag.rationale = rule.rationale;
        result[index].push_back(std::move(flag));
    };

    // 1. structural
    for (const Rule& rule : active) {
        if (rule.type != "structural") continue;
        RulePattern parsed = ParsePattern(rule.pattern);
        for (size_t i = 0; i < items.size(); ++i) {
            if (!SubaccountAllowed(parsed, items[i])) continue;
            double amount = 0.0;
            if (StructuralMatch(items[i], parsed.body, amount)) apply(rule, i, amount);
        }
    }

    // 2. exact duplicates
    for (const Rule& rule : active) {
        if (rule.type != "exact") continue;
        RulePattern parsed = ParsePattern(rule.pattern);
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
        RulePattern parsed = ParsePattern(rule.pattern);
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
    std::unordered_map<std::string, std::vector<std::string>> termsByRule;
    for (const Term& term : terms) {
        if (!term.active || term.ruleCode.empty()) continue;
        std::string norm = term.termNorm.empty() ? ToLowerUtf8(term.term) : term.termNorm;
        if (!norm.empty()) termsByRule[term.ruleCode].push_back(norm);
    }
    for (const Rule& rule : active) {
        if (rule.type != "term") continue;
        RulePattern parsed = ParsePattern(rule.pattern);
        auto it = termsByRule.find(rule.code);
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
        RulePattern parsed = ParsePattern(rule.pattern);
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
