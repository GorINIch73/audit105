#include "XmlReader.h"
#include "Utf8.h"

#include <algorithm>
#include <fstream>
#include <iterator>
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

int IntAttribute(const std::string& tag, const std::string& name, int fallback) {
    std::string value = Attribute(tag, name);
    if (value.empty()) return fallback;
    int parsed = 0;
    bool any = false;
    for (char c : value) {
        if (c < '0' || c > '9') return fallback;
        parsed = parsed * 10 + (c - '0');
        any = true;
    }
    return any ? parsed : fallback;
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

}  // namespace

bool ReadXml(const std::string& path, Spreadsheet& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Не удалось прочитать файл: " + path;
        return false;
    }
    std::string xml{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

    out.source = path;
    out.format = "XML";
    out.rows.clear();

    if (xml.find("<table:table") != std::string::npos) {
        size_t tableStart = xml.find("<table:table");
        if (tableStart != std::string::npos) {
            size_t tableEnd = xml.find('>', tableStart);
            if (tableEnd != std::string::npos) {
                out.sheetName = Attribute(xml.substr(tableStart, tableEnd - tableStart + 1), "table:name");
            }
        }

        int rowIndex = 0;
        size_t pos = tableStart == std::string::npos ? 0 : tableStart;
        while (pos < xml.size()) {
            size_t rowStart = xml.find("<table:table-row", pos);
            if (rowStart == std::string::npos) break;
            size_t rowEnd = xml.find("</table:table-row>", rowStart);
            if (rowEnd == std::string::npos) rowEnd = xml.size();

            Row row;
            row.index = ++rowIndex;

            size_t cursor = rowStart;
            while (cursor < rowEnd) {
                size_t cellStart = xml.find("<table:table-cell", cursor);
                size_t coveredStart = xml.find("<table:covered-table-cell", cursor);
                size_t next = std::min(cellStart, coveredStart);
                if (next == std::string::npos || next >= rowEnd) break;

                bool covered = (next == coveredStart);
                size_t tagEnd = xml.find('>', next);
                if (tagEnd == std::string::npos || tagEnd > rowEnd) break;

                std::string tag = xml.substr(next, tagEnd - next + 1);
                bool selfClosing = tagEnd > next && tag[tagEnd - next - 1] == '/';

                Cell cell;
                cell.covered = covered;
                cell.repeated = IntAttribute(tag, "table:number-columns-repeated", 1);
                cell.span = IntAttribute(tag, "table:number-columns-spanned", 1);
                cell.col = row.width;

                if (!selfClosing) {
                    std::string closing = covered ? "</table:covered-table-cell>" : "</table:table-cell>";
                    size_t close = xml.find(closing, tagEnd + 1);
                    if (close == std::string::npos || close > rowEnd) close = rowEnd;
                    cell.text = TrimUtf8(DecodeEntities(StripTags(xml.substr(tagEnd + 1, close - tagEnd - 1))));
                    cursor = close + closing.size();
                } else {
                    cursor = tagEnd + 1;
                }

                row.width += cell.repeated;
                row.cells.push_back(std::move(cell));
            }

            out.rows.push_back(std::move(row));
            pos = rowEnd + 18;
        }

        if (out.rows.empty()) {
            error = "В XML не найдено ни одной строки";
            return false;
        }
        return true;
    }

    size_t worksheet = xml.find("<Worksheet");
    if (worksheet == std::string::npos) {
        error = "В XML не найден лист (Worksheet или table:table)";
        return false;
    }
    std::string wsTag = xml.substr(worksheet, xml.find('>', worksheet) - worksheet + 1);
    out.sheetName = Attribute(wsTag, "name");

    size_t tableStart = xml.find("<Table", worksheet);
    size_t searchFrom = tableStart == std::string::npos ? worksheet : tableStart;

    int rowIndex = 0;
    size_t pos = searchFrom;
    while (true) {
        size_t rowStart = xml.find("<Row", pos);
        if (rowStart == std::string::npos) break;
        size_t rowEnd = xml.find("</Row>", rowStart);
        if (rowEnd == std::string::npos) rowEnd = xml.size();

        Row row;
        row.index = ++rowIndex;

        size_t cursor = rowStart;
        int nextCol = 0;
        while (true) {
            size_t cellStart = xml.find("<Cell", cursor);
            if (cellStart == std::string::npos || cellStart >= rowEnd) break;
            size_t tagEnd = xml.find('>', cellStart);
            if (tagEnd == std::string::npos || tagEnd > rowEnd) break;

            std::string tag = xml.substr(cellStart, tagEnd - cellStart + 1);
            bool selfClosing = tagEnd > cellStart && tag[tagEnd - cellStart - 1] == '/';

            Cell cell;
            int index = IntAttribute(tag, "ss:Index", 0);
            cell.col = (index > 0) ? index - 1 : nextCol;
            cell.span = IntAttribute(tag, "ss:MergeAcross", 1) + 1;

            if (!selfClosing) {
                size_t close = xml.find("</Cell>", tagEnd + 1);
                if (close == std::string::npos || close > rowEnd) close = rowEnd;
                size_t dataStart = xml.find("<Data", tagEnd + 1);
                if (dataStart != std::string::npos && dataStart < close) {
                    size_t dataEnd = xml.find("</Data>", dataStart);
                    if (dataEnd == std::string::npos || dataEnd > close) dataEnd = close;
                    cell.text = TrimUtf8(DecodeEntities(StripTags(xml.substr(dataStart, dataEnd - dataStart))));
                }
                cursor = close + 7;
            } else {
                cursor = tagEnd + 1;
            }

            nextCol = cell.col + cell.span;
            row.width = nextCol;
            row.cells.push_back(std::move(cell));
        }

        out.rows.push_back(std::move(row));
        pos = rowEnd + 6;
    }

    if (out.rows.empty()) {
        error = "В XML не найдено ни одной строки";
        return false;
    }
    return true;
}
