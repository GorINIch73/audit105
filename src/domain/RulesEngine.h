#pragma once

#include "PositionParser.h"

#include <cstdint>
#include <string>
#include <vector>

struct Rule {
    int64_t id = 0;
    int64_t categoryId = 0;
    std::string code;
    std::string category;    // display only, resolved from categories by categoryId
    std::string type;        // structural | exact | synonym | term | regex
    std::string pattern;
    int severity = 1;
    std::string rationale;
    std::string sourceDoc;
    bool active = true;
};

struct Term {
    int64_t id = 0;
    int64_t ruleId = 0;
    std::string ruleCode;    // display only, resolved from rules by ruleId
    std::string term;
    std::string termNorm;
    std::string scope;
    std::string origin;
    std::string listName;
    std::string sourceDoc;
    std::string asOfDate;
    int confidence = 0;  // 3 = АТХ/ОКПД2, 2 = ФТГ, 1 = текстовая маска, 0 = вручную
    bool active = true;
};

struct FlagResult {
    int64_t ruleId = 0;
    std::string ruleCode;
    int severity = 1;
    double amount = 0.0;
    std::string rationale;
};

// Pattern shape shared by the engine and the editor: "sub:105.31,105.32; <body>".
struct PatternParts {
    std::vector<std::string> subaccounts;
    std::string body;
};

PatternParts ParseRulePattern(const std::string& pattern);
std::string BuildRulePattern(const PatternParts& parts);

struct KeywordHelp {
    std::string name;
    std::string logic;
    std::string example;
    std::string keyword;  // empty when the pattern body is free text
};

std::vector<std::string> StructuralKeywords();
std::vector<std::string> ExactKeywords();
std::vector<KeywordHelp> KeywordHelpFor(const std::string& type);
std::string KeywordName(const std::string& body);
double KeywordThreshold(const std::string& body);

// Returns flags per item, in the order structural -> exact -> synonym -> term -> regex.
std::vector<std::vector<FlagResult>> EvaluateRules(const std::vector<Item>& items,
                                                   const std::vector<Rule>& rules,
                                                   const std::vector<Term>& terms);

std::string ExactSignature(const Item& item);
std::string NameSignature(const Item& item);
