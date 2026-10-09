#pragma once

#include "Spreadsheet.h"

#include <string>
#include <vector>

struct Subaccount {
    std::string code;
    bool isTotal = false;
    double opening = 0.0;
    double debit = 0.0;
    double credit = 0.0;
    double closing = 0.0;
    bool arithOk = true;
};

struct Item {
    int rowNo = 0;
    std::string subaccount;
    std::string nameRaw;
    std::string nameNorm;

    double openDebit = 0.0;
    double openCredit = 0.0;
    double inDebit = 0.0;
    double outCredit = 0.0;
    double closeDebit = 0.0;
    double closeCredit = 0.0;

    double qtyOpen = 0.0;
    double qtyIn = 0.0;
    double qtyOut = 0.0;
    double qtyClose = 0.0;

    double unitPrice = 0.0;
    bool arithOk = true;
    bool slotsInferred = false;
    bool hasQtyRow = false;
    bool qtyMismatch = false;
    int valueCount = 0;
    int qtyCount = 0;
};

struct ParseReport {
    std::vector<Subaccount> subaccounts;
    std::vector<Item> items;

    int sumRows = 0;
    int qtyRows = 0;
    int subtotalRows = 0;
    int arithFail = 0;
    int subtotalArithFail = 0;
    int missingQty = 0;
    int qtyMismatch = 0;
    int duplicates = 0;
    int nameDuplicates = 0;
    int noTurnover = 0;
    int negativeClosing = 0;

    double closingTotal = 0.0;
    double debitTotal = 0.0;
    double creditTotal = 0.0;

    int labelColumn = -1;
    int slotColumns[6] = {0, 0, 0, 0, 0, 0};
};

std::string NormalizeText(const std::string& text);
double ParseNumber(const std::string& text, bool& ok);
bool IsSubaccountCode(const std::string& text);

ParseReport ParsePositions(const Spreadsheet& sheet);
