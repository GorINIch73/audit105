#include "XlsxReader.h"
#include "Utf8.h"
#include "Zip.h"

#include <zlib.h>

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

struct SheetContext {
    std::vector<std::string> shared;
    std::map<std::pair<int, int>, int> spans;
};

// Разбирает один <row>…</row> из XML-буфера. lastRowIndex нужен, когда у строки
// нет атрибута r (Excel пропускает пустые строки).
void ParseRowFromXml(const std::string& xml, size_t rowStart, size_t rowEnd,
                     const SheetContext& ctx, int& lastRowIndex, Row& row) {
    std::string rowTag = xml.substr(rowStart, xml.find('>', rowStart) - rowStart + 1);
    std::string rowRef = Attribute(rowTag, "r");
    int rowNum = lastRowIndex + 1;
    if (!rowRef.empty()) {
        int parsed = 0;
        for (char c : rowRef) {
            if (c >= '0' && c <= '9') parsed = parsed * 10 + (c - '0');
        }
        if (parsed > 0) rowNum = parsed;
    }
    lastRowIndex = rowNum;
    row.index = rowNum;

    size_t cursor = rowStart;
    while (true) {
        size_t cellStart = xml.find("<c ", cursor);
        if (cellStart == std::string::npos || cellStart >= rowEnd) break;
        size_t tagEnd = xml.find('>', cellStart);
        if (tagEnd == std::string::npos || tagEnd > rowEnd) break;

        std::string tag = xml.substr(cellStart, tagEnd - cellStart + 1);
        bool selfClosing = tagEnd > cellStart && tag[tagEnd - cellStart - 1] == '/';

        Cell cell;
        std::string ref = Attribute(tag, "r");
        cell.col = ref.empty() ? row.width : ColumnIndex(ref);
        auto spanIt = ctx.spans.find({rowNum, cell.col});
        cell.span = (spanIt != ctx.spans.end()) ? spanIt->second : 1;

        if (!selfClosing) {
            size_t close = xml.find("</c>", tagEnd + 1);
            if (close == std::string::npos || close > rowEnd) close = rowEnd;
            std::string inner = xml.substr(tagEnd + 1, close - tagEnd - 1);
            std::string type = Attribute(tag, "t");
            if (type == "s") {
                size_t v = inner.find("<v>");
                if (v != std::string::npos) {
                    size_t vEnd = inner.find("</v>", v);
                    long idx = std::stol(inner.substr(v + 3, vEnd - v - 3), nullptr, 10);
                    if (idx >= 0 && idx < static_cast<long>(ctx.shared.size())) {
                        cell.text = ctx.shared[static_cast<size_t>(idx)];
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
}

std::string ResolveSheetName(const std::vector<XlsxSheet>& sheets,
                             const std::vector<zip::Entry>& entries,
                             const zip::Entry* sheet, const std::string& requested) {
    if (!requested.empty()) return requested;
    for (const XlsxSheet& s : sheets) {
        if (zip::FindEntry(entries, s.entry) == sheet) return s.name;
    }
    return "Лист";
}

const zip::Entry* PickSheetEntry(const std::vector<zip::Entry>& entries,
                                 const std::vector<XlsxSheet>& sheets,
                                 const std::string& sheetName) {
    if (!sheetName.empty()) {
        for (const XlsxSheet& s : sheets) {
            if (s.name == sheetName) return zip::FindEntry(entries, s.entry);
        }
        return nullptr;
    }
    if (!sheets.empty()) {
        const XlsxSheet* best = &sheets[0];
        for (const XlsxSheet& s : sheets) {
            if (s.size > best->size) best = &s;
        }
        const zip::Entry* found = zip::FindEntry(entries, best->entry);
        if (found) return found;
    }
    const zip::Entry* fallback = zip::FindEntry(entries, "xl/worksheets/sheet1.xml");
    if (fallback) return fallback;
    for (const zip::Entry& e : entries) {
        if (e.name.size() > 16 && e.name.compare(0, 16, "xl/worksheets/") == 0) return &e;
    }
    return nullptr;
}

std::vector<XlsxSheet> CollectSheets(const std::string& zipData,
                                     const std::vector<zip::Entry>& entries) {
    std::vector<XlsxSheet> sheets;
    const zip::Entry* workbook = zip::FindEntry(entries, "xl/workbook.xml");
    if (!workbook) return sheets;

    std::string wbXml;
    if (!zip::ExtractEntry(zipData, *workbook, wbXml)) return sheets;

    std::map<std::string, std::string> relTarget;
    const zip::Entry* rels = zip::FindEntry(entries, "xl/_rels/workbook.xml.rels");
    if (rels) {
        std::string relsXml;
        if (zip::ExtractEntry(zipData, *rels, relsXml)) {
            size_t pos = 0;
            while (true) {
                size_t r = relsXml.find("<Relationship ", pos);
                if (r == std::string::npos) break;
                std::string tag = relsXml.substr(r, relsXml.find('>', r) - r + 1);
                std::string id = Attribute(tag, "Id");
                std::string target = Attribute(tag, "Target");
                if (!id.empty() && !target.empty()) relTarget[id] = target;
                pos = r + 13;
            }
        }
    }

    size_t pos = 0;
    while (true) {
        size_t s = wbXml.find("<sheet ", pos);
        if (s == std::string::npos) break;
        std::string tag = wbXml.substr(s, wbXml.find('>', s) - s + 1);
        XlsxSheet sheet;
        sheet.name = Attribute(tag, "name");
        std::string rid = Attribute(tag, "r:id");
        auto it = relTarget.find(rid);
        if (it != relTarget.end()) {
            std::string target = it->second;
            if (target.compare(0, 4, "xl/") != 0) target = "xl/" + target;
            sheet.entry = target;
        } else {
            sheet.entry = "xl/worksheets/sheet" + std::to_string(sheets.size() + 1) + ".xml";
        }
        const zip::Entry* entry = zip::FindEntry(entries, sheet.entry);
        sheet.size = entry ? entry->uncompSize : 0;
        sheets.push_back(std::move(sheet));
        pos = s + 7;
    }
    return sheets;
}


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

bool ListXlsxSheets(const std::string& path, std::vector<XlsxSheet>& sheets, std::string& error) {
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
    sheets = CollectSheets(zipData, zip::ReadEntries(zipData, cdOffset, cdSize));
    if (sheets.empty()) {
        error = "В XLSX не найдено ни одного листа";
        return false;
    }
    return true;
}

bool ReadXlsx(const std::string& path, Spreadsheet& out, std::string& error,
              const std::string& sheetName) {
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
    std::vector<XlsxSheet> sheets = CollectSheets(zipData, entries);

    const zip::Entry* sheet = PickSheetEntry(entries, sheets, sheetName);
    if (!sheet) {
        error = sheetName.empty()
                    ? "В XLSX не найден лист (xl/worksheets/sheet1.xml)"
                    : "Лист \"" + sheetName + "\" не найден в файле";
        return false;
    }

    std::string sheetXml;
    if (!zip::ExtractEntry(zipData, *sheet, sheetXml)) {
        error = "Не удалось распаковать лист XLSX";
        return false;
    }

    SheetContext ctx;
    const zip::Entry* shared = zip::FindEntry(entries, "xl/sharedStrings.xml");
    if (shared) {
        std::string sharedXml;
        if (zip::ExtractEntry(zipData, *shared, sharedXml)) {
            ctx.shared = ParseSharedStrings(sharedXml);
        }
    }
    for (const Merge& m : ParseMerges(sheetXml)) {
        ctx.spans[{m.row, m.colStart}] = m.colEnd - m.colStart + 1;
    }

    out.source = path;
    out.format = "XLSX";
    out.sheetName = ResolveSheetName(sheets, entries, sheet, sheetName);
    out.rows.clear();

    int rowIndex = 0;
    size_t pos = 0;
    while (true) {
        size_t rowStart = sheetXml.find("<row", pos);
        if (rowStart == std::string::npos) break;
        size_t rowEnd = sheetXml.find("</row>", rowStart);
        if (rowEnd == std::string::npos) rowEnd = sheetXml.size();

        Row row;
        ParseRowFromXml(sheetXml, rowStart, rowEnd, ctx, rowIndex, row);
        out.rows.push_back(std::move(row));
        pos = rowEnd + 6;
    }

    if (out.rows.empty()) {
        error = "В XLSX не найдено ни одной строки";
        return false;
    }
    return true;
}

// Вычитывает из буфера все полностью полученные <row> и отдаёт их visitor'у.
// Буфер обрезается по последнюю разобранныю строку, поэтому в памяти живёт
// не больше одного chunk'а XML.
bool FlushRows(std::string& pending, SheetContext& ctx, int& lastRowIndex,
               const std::function<bool(const Row&)>& visitor, std::string& error) {
    size_t firstRow = pending.find("<row");
    if (firstRow != std::string::npos) {
        for (size_t at = pending.find("<mergeCell ", 0);
             at != std::string::npos && at < firstRow;
             at = pending.find("<mergeCell ", at + 10)) {
            std::string ref = Attribute(pending.substr(at, pending.find('>', at) - at + 1), "ref");
            size_t colon = ref.find(':');
            if (colon == std::string::npos) continue;
            int rowNum = 0;
            for (char c : ref.substr(0, colon)) {
                if (c >= '0' && c <= '9') {
                    rowNum = rowNum * 10 + (c - '0');
                    break;
                }
            }
            ctx.spans[{rowNum, ColumnIndex(ref.substr(0, colon))}] =
                ColumnIndex(ref.substr(colon + 1)) - ColumnIndex(ref.substr(0, colon)) + 1;
        }
    }

    size_t consumed = 0;
    while (true) {
        size_t rowStart = pending.find("<row", consumed);
        if (rowStart == std::string::npos) break;
        size_t tagEnd = pending.find('>', rowStart);
        if (tagEnd == std::string::npos) break;
        bool selfClosing = tagEnd > rowStart && pending[tagEnd - 1] == '/';

        size_t rowEnd;
        if (selfClosing) {
            rowEnd = tagEnd + 1;
        } else {
            size_t close = pending.find("</row>", tagEnd);
            if (close == std::string::npos) break;  // строка ещё не дошла целиком
            rowEnd = close + 6;
        }

        Row row;
        ParseRowFromXml(pending, rowStart, rowEnd, ctx, lastRowIndex, row);
        if (!visitor(row)) {
            pending.erase(0, rowEnd);
            return false;
        }
        consumed = rowEnd;
    }
    if (consumed > 0) pending.erase(0, consumed);
    (void)error;
    return true;
}

static int DimensionMaxRow(const std::string& xml) {
    size_t at = xml.find("<dimension");
    if (at == std::string::npos) return 0;
    size_t ref = xml.find("ref=\"", at);
    if (ref == std::string::npos) return 0;
    ref += 5;

    int best = 0;
    int current = 0;
    for (size_t i = ref; i < xml.size() && xml[i] != '"'; ++i) {
        if (xml[i] >= '0' && xml[i] <= '9') {
            current = current * 10 + (xml[i] - '0');
        } else {
            if (current > best) best = current;
            current = 0;
        }
    }
    if (current > best) best = current;
    return best;
}

bool StreamXlsxRows(const std::string& path, const std::string& sheetName,
                    const std::function<bool(const Row&)>& visitor, std::string& error,
                    int* totalRowsOut) {
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
    std::vector<XlsxSheet> sheets = CollectSheets(zipData, entries);

    const zip::Entry* sheet = PickSheetEntry(entries, sheets, sheetName);
    if (!sheet) {
        error = sheetName.empty()
                    ? "В XLSX не найден лист (xl/worksheets/sheet1.xml)"
                    : "Лист \"" + sheetName + "\" не найден в файле";
        return false;
    }

    size_t from = 0, length = 0;
    if (!zip::EntryRange(zipData, *sheet, from, length)) {
        error = "Не удалось найти данные листа в ZIP";
        return false;
    }

    SheetContext ctx;
    const zip::Entry* shared = zip::FindEntry(entries, "xl/sharedStrings.xml");
    if (shared) {
        std::string sharedXml;
        if (zip::ExtractEntry(zipData, *shared, sharedXml)) {
            ctx.shared = ParseSharedStrings(sharedXml);
        }
    }

    if (sheet->method == 0) {
        std::string xml(zipData.begin() + from, zipData.begin() + from + length);
        if (totalRowsOut) *totalRowsOut = DimensionMaxRow(xml);
        int lastRowIndex = 0;
        return FlushRows(xml, ctx, lastRowIndex, visitor, error);
    }
    if (sheet->method != 8) {
        error = "Неизвестный метод сжатия элемента XLSX";
        return false;
    }

    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK) {
        error = "Не удалось инициализировать inflate";
        return false;
    }

    const size_t kChunk = 1 << 16;
    std::vector<char> out(kChunk);
    std::string pending;
    pending.reserve(kChunk * 2);
    int lastRowIndex = 0;
    size_t inPos = 0;
    bool ok = true;

    while (true) {
        if (stream.avail_in == 0) {
            if (inPos >= length) break;
            size_t take = std::min(kChunk, length - inPos);
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zipData.data()) + from + inPos);
            stream.avail_in = static_cast<uInt>(take);
            inPos += take;
        }
        stream.next_out = reinterpret_cast<Bytef*>(out.data());
        stream.avail_out = static_cast<uInt>(out.size());
        int result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            error = "Ошибка распаковки листа XLSX";
            ok = false;
            break;
        }
        pending.append(out.data(), out.size() - stream.avail_out);
        if (totalRowsOut && *totalRowsOut == 0) *totalRowsOut = DimensionMaxRow(pending);
        if (!FlushRows(pending, ctx, lastRowIndex, visitor, error)) {
            ok = false;
            break;
        }
        if (result == Z_STREAM_END) break;
    }

    if (ok) ok = FlushRows(pending, ctx, lastRowIndex, visitor, error);
    inflateEnd(&stream);
    return ok;
}
