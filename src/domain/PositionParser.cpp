#include "PositionParser.h"
#include "Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const char* kSumLabel = u8"Сумма";
const char* kQtyLabel = u8"Кол.";
const char* kDebitLabel = u8"Дебет";
const char* kCreditLabel = u8"Кредит";

bool SignSearch(const std::vector<double>& values, std::vector<double>& assigned) {
    if (values.empty() || values.size() > 6) return false;
    for (int mask = 0; mask < (1 << static_cast<int>(values.size())); ++mask) {
        double sum = 0.0;
        for (size_t i = 0; i < values.size(); ++i) {
            sum += (mask & (1 << i)) ? values[i] : -values[i];
        }
        if (std::fabs(sum) < 0.01) {
            assigned = values;
            for (size_t i = 0; i < assigned.size(); ++i) {
                if (!(mask & (1 << i))) assigned[i] = -assigned[i];
            }
            return true;
        }
    }
    return false;
}

double Median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

}  // namespace

std::string NormalizeText(const std::string& text) {
    return NormalizeTextUtf8(text);
}

double ParseNumber(const std::string& text, bool& ok) {
    ok = false;
    std::string cleaned;
    cleaned.reserve(text.size());

    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        if (IsUtf8Space(cp)) continue;
        if (cp == ',') {
            cleaned.push_back('.');
            continue;
        }
        if (cp == 0x2212) {
            cleaned.push_back('-');
            continue;
        }
        if (cp >= 0x80) return 0.0;
        cleaned.push_back(static_cast<char>(cp));
    }

    if (cleaned.empty()) return 0.0;
    try {
        size_t consumed = 0;
        double value = std::stod(cleaned, &consumed);
        if (consumed == cleaned.size()) {
            ok = true;
            return value;
        }
    } catch (...) {
    }
    return 0.0;
}

bool IsSubaccountCode(const std::string& text) {
    int digitsBefore = 0;
    int dot = -1;
    int digitsAfter = 0;
    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        if (cp >= '0' && cp <= '9') {
            if (dot < 0) ++digitsBefore;
            else ++digitsAfter;
        } else if (cp == '.') {
            if (dot >= 0) return false;
            dot = static_cast<int>(index);
        } else {
            return false;
        }
    }
    return dot > 0 && digitsBefore >= 1 && digitsBefore <= 4 && digitsAfter >= 1 && digitsAfter <= 2;
}

ParseReport ParsePositions(const Spreadsheet& sheet) {
    ParseReport report;

    int labelCol = -1;
    std::vector<int> debitCols;
    std::vector<int> creditCols;

    for (const Row& row : sheet.rows) {
        std::vector<int> rowDebit;
        std::vector<int> rowCredit;
        int rowLabel = -1;
        for (const Cell& cell : row.cells) {
            if (cell.covered) continue;
            if (EqualsUtf8Insensitive(cell.text, kSumLabel)) {
                if (rowLabel < 0) rowLabel = cell.col;
            } else if (EqualsUtf8Insensitive(cell.text, kDebitLabel)) {
                rowDebit.push_back(cell.col);
            } else if (EqualsUtf8Insensitive(cell.text, kCreditLabel)) {
                rowCredit.push_back(cell.col);
            }
        }
        if (rowLabel >= 0 && rowDebit.size() >= 3 && rowCredit.size() >= 3) {
            labelCol = rowLabel;
            debitCols = rowDebit;
            creditCols = rowCredit;
            break;
        }
    }

    int slots[6];
    if (debitCols.size() >= 3 && creditCols.size() >= 3) {
        slots[0] = debitCols[0];
        slots[1] = creditCols[0];
        slots[2] = debitCols[1];
        slots[3] = creditCols[1];
        slots[4] = debitCols[2];
        slots[5] = creditCols[2];
    } else {
        slots[0] = 7;
        slots[1] = 10;
        slots[2] = 14;
        slots[3] = 17;
        slots[4] = 20;
        slots[5] = 23;
    }
    if (labelCol < 0) labelCol = 4;
    report.labelColumn = labelCol;
    std::memcpy(report.slotColumns, slots, sizeof(slots));

    auto readSlots = [&](const Row& row, double out[6]) {
        int count = 0;
        for (int i = 0; i < 6; ++i) {
            const Cell* cell = row.CellAt(slots[i]);
            if (!cell) {
                out[i] = 0.0;
                continue;
            }
            bool ok = false;
            out[i] = ParseNumber(cell->text, ok);
            if (ok) ++count;
        }
        return count;
    };

    std::string currentSub;
    int pendingItem = -1;

    for (const Row& row : sheet.rows) {
        const std::string first = TrimUtf8(row.TextAt(0));
        const std::string label = TrimUtf8(row.TextAt(labelCol));

        if (IsSubaccountCode(first)) {
            double values[6];
            int count = readSlots(row, values);

            Subaccount sub;
            sub.code = first;
            sub.isTotal = true;
            sub.opening = values[0] - values[1];
            sub.debit = values[2];
            sub.credit = values[3];
            sub.closing = values[4] - values[5];
            sub.arithOk = std::fabs(sub.opening + sub.debit - sub.credit - sub.closing) < 0.01;
            if (!sub.arithOk) ++report.subtotalArithFail;
            if (count > 0) ++report.subtotalRows;
            report.subaccounts.push_back(sub);
            currentSub = first;
            pendingItem = -1;
            continue;
        }

        if (EqualsUtf8Insensitive(label, kSumLabel)) {
            ++report.sumRows;

            Item item;
            item.rowNo = row.index;
            item.subaccount = currentSub;
            item.nameRaw = first;
            item.nameNorm = NormalizeTextUtf8(first);

            double values[6];
            item.valueCount = readSlots(row, values);
            item.openDebit = values[0];
            item.openCredit = values[1];
            item.inDebit = values[2];
            item.outCredit = values[3];
            item.closeDebit = values[4];
            item.closeCredit = values[5];

            double lhs = item.openDebit - item.openCredit + item.inDebit - item.outCredit;
            double rhs = item.closeDebit - item.closeCredit;
            item.arithOk = std::fabs(lhs - rhs) < 0.01;

            if (!item.arithOk) {
                std::vector<double> present;
                for (int i = 0; i < 6; ++i) {
                    if (values[i] != 0.0) present.push_back(values[i]);
                }
                std::vector<double> assigned;
                if (SignSearch(present, assigned)) {
                    item.arithOk = true;
                    item.slotsInferred = true;
                } else {
                    ++report.arithFail;
                }
            }

            report.items.push_back(item);
            pendingItem = static_cast<int>(report.items.size()) - 1;
            continue;
        }

        if (EqualsUtf8Insensitive(label, kQtyLabel)) {
            ++report.qtyRows;
            if (pendingItem >= 0) {
                double values[6];
                int count = readSlots(row, values);

                Item& item = report.items[pendingItem];
                item.hasQtyRow = true;
                item.qtyCount = count;
                item.qtyOpen = values[0] - values[1];
                item.qtyIn = values[2] - values[3];
                item.qtyClose = values[4] - values[5];
                item.qtyOut = item.qtyOpen + item.qtyIn - item.qtyClose;
                item.qtyMismatch = (count != item.valueCount);
                if (item.qtyMismatch) ++report.qtyMismatch;

                std::vector<double> prices;
                auto addPrice = [&](double amount, double qty) {
                    if (std::fabs(qty) > 1e-9 && std::fabs(amount) > 1e-9) {
                        prices.push_back(std::fabs(amount / qty));
                    }
                };
                addPrice(item.openDebit - item.openCredit, item.qtyOpen);
                addPrice(item.inDebit - item.outCredit, item.qtyIn);
                addPrice(item.closeDebit - item.closeCredit, item.qtyClose);
                item.unitPrice = Median(prices);

                pendingItem = -1;
            } else {
                ++report.missingQty;
            }
            continue;
        }

        pendingItem = -1;
    }

    std::unordered_map<std::string, int> exact;
    std::unordered_map<std::string, int> byName;
    for (const Item& item : report.items) {
        std::string key = item.nameNorm + "|" + item.subaccount;
        ++byName[key];

        std::ostringstream signature;
        signature << key << '#'
                  << std::fixed << std::setprecision(2)
                  << item.openDebit << ',' << item.openCredit << ','
                  << item.inDebit << ',' << item.outCredit << ','
                  << item.closeDebit << ',' << item.closeCredit;
        ++exact[signature.str()];

        if (item.inDebit == 0.0 && item.outCredit == 0.0 && (item.closeDebit - item.closeCredit) != 0.0) {
            ++report.noTurnover;
        }
        if (item.closeDebit - item.closeCredit < 0.0) ++report.negativeClosing;

        report.closingTotal += item.closeDebit - item.closeCredit;
        report.debitTotal += item.inDebit;
        report.creditTotal += item.outCredit;
    }
    for (const auto& entry : exact) {
        if (entry.second > 1) report.duplicates += entry.second - 1;
    }
    for (const auto& entry : byName) {
        if (entry.second > 1) report.nameDuplicates += entry.second - 1;
    }

    return report;
}
