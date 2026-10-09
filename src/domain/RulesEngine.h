#pragma once

#include "PositionParser.h"

#include <cstdint>
#include <string>
#include <vector>

struct Rule {
    int64_t id = 0;
    std::string code;
    std::string category;
    std::string type;        // structural | exact | synonym | term | regex
    std::string pattern;
    int severity = 1;
    std::string rationale;
    std::string sourceDoc;
    bool active = true;
};

struct Term {
    int64_t id = 0;
    std::string ruleCode;
    std::string term;
    std::string termNorm;
    std::string scope;
    std::string origin;
    std::string listName;
    bool active = true;
};

struct FlagResult {
    std::string ruleCode;
    std::string category;
    int severity = 1;
    double amount = 0.0;
    std::string rationale;
};

// Returns flags per item, in the order structural -> exact -> synonym -> term -> regex.
std::vector<std::vector<FlagResult>> EvaluateRules(const std::vector<Item>& items,
                                                   const std::vector<Rule>& rules,
                                                   const std::vector<Term>& terms);

std::string ExactSignature(const Item& item);
std::string NameSignature(const Item& item);
