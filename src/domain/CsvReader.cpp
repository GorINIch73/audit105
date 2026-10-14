#include "CsvReader.h"
#include "Utf8.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

char DetectDelimiter(const std::string& text) {
    size_t lineEnd = text.find('\n');
    std::string first = text.substr(0, lineEnd == std::string::npos ? text.size() : lineEnd);
    int commas = 0, semicolons = 0, tabs = 0;
    bool inQuotes = false;
    for (char c : first) {
        if (c == '"') inQuotes = !inQuotes;
        if (inQuotes) continue;
        if (c == ',') ++commas;
        else if (c == ';') ++semicolons;
        else if (c == '\t') ++tabs;
    }
    if (tabs >= commas && tabs >= semicolons && tabs > 0) return '\t';
    if (semicolons > commas) return ';';
    if (commas > 0) return ',';
    return ';';
}

std::vector<std::string> SplitLine(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
                continue;
            }
            inQuotes = !inQuotes;
            continue;
        }
        if (c == delimiter && !inQuotes) {
            fields.push_back(field);
            field.clear();
            continue;
        }
        field.push_back(c);
    }
    fields.push_back(field);
    return fields;
}

}  // namespace

bool ReadCsv(const std::string& path, Spreadsheet& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Не удалось прочитать файл: " + path;
        return false;
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (text.size() >= 3 && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF') {
        text.erase(0, 3);
    }
    if (text.empty()) {
        error = "Файл пуст: " + path;
        return false;
    }

    const char delimiter = DetectDelimiter(text);

    out.source = path;
    out.format = "CSV";
    out.sheetName = "CSV";
    out.rows.clear();

    std::string line;
    size_t pos = 0;
    int rowIndex = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            line = text.substr(pos);
            pos = text.size() + 1;
        } else {
            line = text.substr(pos, end - pos);
            pos = end + 1;
        }
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::vector<std::string> fields = SplitLine(line, delimiter);
        Row row;
        row.index = ++rowIndex;
        row.width = static_cast<int>(fields.size());
        for (size_t i = 0; i < fields.size(); ++i) {
            Cell cell;
            cell.col = static_cast<int>(i);
            cell.text = TrimUtf8(fields[i]);
            row.cells.push_back(std::move(cell));
        }
        out.rows.push_back(std::move(row));
    }

    if (out.rows.empty()) {
        error = "В CSV не найдено ни одной строки";
        return false;
    }
    return true;
}
