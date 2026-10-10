#include "XlsxReader.h"
#include "Utf8.h"
#include "Zip.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

std::string Attribute(const std::string& tag, const std::string& name) {
    size_t at = tag.find(name + "=\"");
    if (at == std::string::npos) return std::string();
    at += name.size() + 2;
    size_t end = tag.find('"', at);
    if (end == std::string::npos) return std::string();
    return tag.substr(at, end - at);
}

std::string DecodeEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        size_t semi = text.find(';', i);
        if (semi == std::string::npos || semi > i + 8) {
            out.push_back(text[i]);
            continue;
        }
        std::string code = text.substr(i + 1, semi - i - 1);
        if (code == "amp") out.push_back('&');
        else if (code == "lt") out.push_back('<');
        else if (code == "gt") out.push_back('>');
        else if (code == "quot") out.push_back('"');
        else if (code == "apos") out.push_back('\'');
        else if (!code.empty() && code[0] == '#') {
            long cp = (code.size() > 1 && (code[1] == 'x' || code[1] == 'X'))
                          ? std::stol(code.substr(2), nullptr, 16)
                          : std::stol(code.substr(1));
            if (cp < 0x80) out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        } else {
            out.push_back(text[i]);
            continue;
        }
        i = semi;
    }
    return out;
}

std::string StripTags(const std::string& chunk) {
    std::string out;
    out.reserve(chunk.size());
    for (size_t i = 0; i < chunk.size();) {
        if (chunk[i] == '<') {
            size_t end = chunk.find('>', i);
            i = (end == std::string::npos) ? chunk.size() : end + 1;
        } else {
            out.push_back(chunk[i++]);
        }
    }
    return out;
}

int ColumnIndex(const std::string& ref) {
    int col = 0;
    for (char c : ref) {
        if (c >= 'A' && c <= 'Z') col = col * 26 + (c - 'A' + 1);
        else if (c >= 'a' && c <= 'z') col = col * 26 + (c - 'a' + 1);
        else break;
    }
    return col > 0 ? col - 1 : 0;
}

std::vector<std::string> ParseSharedStrings(const std::string& xml) {
    std::vector<std::string> strings;
    size_t pos = 0;
    while (true) {
        size_t si = xml.find("<si", pos);
        if (si == std::string::npos) break;
        size_t end = xml.find("</si>", si);
        if (end == std::string::npos) break;
        std::string inner = xml.substr(si, end - si);
        strings.push_back(TrimUtf8(DecodeEntities(StripTags(inner))));
        pos = end + 5;
    }
    return strings;
}

struct Merge {
    int row;
    int colStart;
    int colEnd;
};

std::vector<Merge> ParseMerges(const std::string& xml) {
    std::vector<Merge> merges;
    size_t pos = 0;
    while (true) {
        size_t mc = xml.find("<mergeCell ", pos);
        if (mc == std::string::npos) break;
        std::string ref = Attribute(xml.substr(mc, xml.find('>', mc) - mc + 1), "ref");
        size_t colon = ref.find(':');
        if (colon != std::string::npos) {
            Merge m;
            std::string a = ref.substr(0, colon);
            std::string b = ref.substr(colon + 1);
            m.row = 0;
            for (char c : a) {
                if (c >= '0' && c <= '9') {
                    m.row = m.row * 10 + (c - '0');
                    break;
                }
            }
            m.colStart = ColumnIndex(a);
            m.colEnd = ColumnIndex(b);
            merges.push_back(m);
        }
        pos = mc + 10;
    }
    return merges;
}

}  // namespace

bool ReadXlsx(const std::string& path, Spreadsheet& out, std::string& error) {
    std::string zipData;
    if (!zip::ReadWholeFile(path, zipData)) {
        error = "Не удалось прочитать файл: " + path;
        return false;
    }

    size_t cdOffset = 0, cdSize = 0;
    if (!zip::FindCentralDirectory(zipData, cdOffset, cdSize)) {
        error = "Не найден центральный каталог ZIP (файл повреждён или не является XLSX)";
        return false;
    }

    std::vector<zip::Entry> entries = zip::ReadEntries(zipData, cdOffset, cdSize);
    const zip::Entry* sheet = zip::FindEntry(entries, "xl/worksheets/sheet1.xml");
    if (!sheet) {
        for (const zip::Entry& e : entries) {
            if (e.name.size() > 18 && e.name.compare(0, 16, "xl/worksheets/") == 0) {
                sheet = &e;
                break;
            }
        }
    }
    if (!sheet) {
        error = "В XLSX не найден лист (xl/worksheets/sheet1.xml)";
        return false;
    }

    std::string sheetXml;
    if (!zip::ExtractEntry(zipData, *sheet, sheetXml)) {
        error = "Не удалось распаковать лист XLSX";
        return false;
    }

    std::vector<std::string> sharedStrings;
    const zip::Entry* shared = zip::FindEntry(entries, "xl/sharedStrings.xml");
    if (shared) {
        std::string sharedXml;
        if (zip::ExtractEntry(zipData, *shared, sharedXml)) {
            sharedStrings = ParseSharedStrings(sharedXml);
        }
    }

    std::string sheetName;
    const zip::Entry* workbook = zip::FindEntry(entries, "xl/workbook.xml");
    if (workbook) {
        std::string wbXml;
        if (zip::ExtractEntry(zipData, *workbook, wbXml)) {
            size_t s = wbXml.find("<sheet ");
            if (s != std::string::npos) {
                sheetName = Attribute(wbXml.substr(s, wbXml.find('>', s) - s + 1), "name");
            }
        }
    }

    std::map<std::pair<int, int>, int> spanByCell;
    for (const Merge& m : ParseMerges(sheetXml)) {
        spanByCell[{m.row, m.colStart}] = m.colEnd - m.colStart + 1;
    }

    out.source = path;
    out.format = "XLSX";
    out.sheetName = sheetName;
    out.rows.clear();

    int rowIndex = 0;
    size_t pos = 0;
    while (true) {
        size_t rowStart = sheetXml.find("<row", pos);
        if (rowStart == std::string::npos) break;
        size_t rowEnd = sheetXml.find("</row>", rowStart);
        if (rowEnd == std::string::npos) rowEnd = sheetXml.size();

        std::string rowTag = sheetXml.substr(rowStart, sheetXml.find('>', rowStart) - rowStart + 1);
        std::string rowRef = Attribute(rowTag, "r");
        int rowNum = rowIndex + 1;
        if (!rowRef.empty()) {
            int parsed = 0;
            for (char c : rowRef) {
                if (c >= '0' && c <= '9') parsed = parsed * 10 + (c - '0');
            }
            if (parsed > 0) rowNum = parsed;
        }

        Row row;
        row.index = rowNum;
        rowIndex = rowNum;

        size_t cursor = rowStart;
        while (true) {
            size_t cellStart = sheetXml.find("<c ", cursor);
            if (cellStart == std::string::npos || cellStart >= rowEnd) break;
            size_t tagEnd = sheetXml.find('>', cellStart);
            if (tagEnd == std::string::npos || tagEnd > rowEnd) break;

            std::string tag = sheetXml.substr(cellStart, tagEnd - cellStart + 1);
            bool selfClosing = tagEnd > cellStart && tag[tagEnd - cellStart - 1] == '/';

            Cell cell;
            std::string ref = Attribute(tag, "r");
            cell.col = ref.empty() ? row.width : ColumnIndex(ref);
            auto spanIt = spanByCell.find({rowNum, cell.col});
            cell.span = (spanIt != spanByCell.end()) ? spanIt->second : 1;

            if (!selfClosing) {
                size_t close = sheetXml.find("</c>", tagEnd + 1);
                if (close == std::string::npos || close > rowEnd) close = rowEnd;
                std::string inner = sheetXml.substr(tagEnd + 1, close - tagEnd - 1);
                std::string type = Attribute(tag, "t");
                if (type == "s") {
                    size_t v = inner.find("<v>");
                    if (v != std::string::npos) {
                        size_t vEnd = inner.find("</v>", v);
                        long idx = std::stol(inner.substr(v + 3, vEnd - v - 3), nullptr, 10);
                        if (idx >= 0 && idx < static_cast<long>(sharedStrings.size())) {
                            cell.text = sharedStrings[static_cast<size_t>(idx)];
                        }
                    }
                } else if (type == "inlineStr") {
                    cell.text = TrimUtf8(DecodeEntities(StripTags(inner)));
                } else {
                    size_t v = inner.find("<v>");
                    if (v != std::string::npos) {
                        size_t vEnd = inner.find("</v>", v);
                        cell.text = TrimUtf8(DecodeEntities(inner.substr(v + 3, vEnd - v - 3)));
                    }
                }
                cursor = close + 4;
            } else {
                cursor = tagEnd + 1;
            }

            row.width = cell.col + cell.span;
            row.cells.push_back(std::move(cell));
        }

        out.rows.push_back(std::move(row));
        pos = rowEnd + 6;
    }

    if (out.rows.empty()) {
        error = "В XLSX не найдено ни одной строки";
        return false;
    }
    return true;
}
