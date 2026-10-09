#include "OdsReader.h"
#include "Utf8.h"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct ZipEntry {
    std::string name;
    uint32_t method = 0;
    uint64_t compSize = 0;
    uint64_t uncompSize = 0;
    uint64_t localOffset = 0;
};

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

uint32_t ReadU32(const std::string& data, size_t at) {
    if (at + 4 > data.size()) return 0;
    return static_cast<uint32_t>(
        static_cast<unsigned char>(data[at]) |
        (static_cast<unsigned int>(static_cast<unsigned char>(data[at + 1])) << 8) |
        (static_cast<unsigned int>(static_cast<unsigned char>(data[at + 2])) << 16) |
        (static_cast<unsigned long long>(static_cast<unsigned char>(data[at + 3])) << 24));
}

uint16_t ReadU16(const std::string& data, size_t at) {
    if (at + 2 > data.size()) return 0;
    return static_cast<uint16_t>(
        static_cast<unsigned char>(data[at]) |
        (static_cast<unsigned int>(static_cast<unsigned char>(data[at + 1])) << 8));
}

bool FindCentralDirectory(const std::string& data, size_t& cdOffset, size_t& cdSize) {
    if (data.size() < 22) return false;
    size_t lower = data.size() > 65557 ? data.size() - 65557 : 0;
    for (size_t i = data.size() - 22 + 1; i > lower; --i) {
        size_t at = i - 1;
        if (data[at] == 'P' && data[at + 1] == 'K' && data[at + 2] == 0x05 && data[at + 3] == 0x06) {
            cdSize = ReadU32(data, at + 12);
            cdOffset = ReadU32(data, at + 16);
            return true;
        }
    }
    return false;
}

std::vector<ZipEntry> ReadEntries(const std::string& data, size_t cdOffset, size_t cdSize) {
    std::vector<ZipEntry> entries;
    size_t pos = cdOffset;
    size_t end = cdOffset + cdSize;
    while (pos + 46 <= end) {
        if (data[pos] != 'P' || data[pos + 1] != 'K' || data[pos + 2] != 0x01 || data[pos + 3] != 0x02) break;
        ZipEntry e;
        e.method = ReadU16(data, pos + 10);
        e.compSize = ReadU32(data, pos + 20);
        e.uncompSize = ReadU32(data, pos + 24);
        size_t nameLen = ReadU16(data, pos + 28);
        size_t extraLen = ReadU16(data, pos + 30);
        size_t commentLen = ReadU16(data, pos + 32);
        e.localOffset = ReadU32(data, pos + 42);
        if (pos + 46 + nameLen > data.size()) break;
        e.name = data.substr(pos + 46, nameLen);
        entries.push_back(std::move(e));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return entries;
}

bool InflateRaw(const std::string& data, size_t from, size_t length, size_t hint, std::string& out) {
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK) return false;

    out.clear();
    if (hint > 0) out.reserve(hint);

    std::vector<char> buffer(1 << 16);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()) + from);
    stream.avail_in = static_cast<uInt>(length);

    int result = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return false;
        }
        out.append(buffer.data(), buffer.size() - stream.avail_out);
    } while (result != Z_STREAM_END && stream.avail_in > 0);

    inflateEnd(&stream);
    return result == Z_STREAM_END;
}

bool ExtractEntry(const std::string& data, const ZipEntry& entry, std::string& out) {
    size_t header = entry.localOffset;
    if (header + 30 > data.size()) return false;
    if (data[header] != 'P' || data[header + 1] != 'K' || data[header + 2] != 0x03 || data[header + 3] != 0x04)
        return false;
    size_t nameLen = ReadU16(data, header + 26);
    size_t extraLen = ReadU16(data, header + 28);
    size_t body = header + 30 + nameLen + extraLen;
    if (body + entry.compSize > data.size()) return false;

    if (entry.method == 0) {
        out.assign(data.begin() + body, data.begin() + body + entry.compSize);
        return true;
    }
    if (entry.method == 8) {
        return InflateRaw(data, body, entry.compSize, entry.uncompSize, out);
    }
    return false;
}

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
    for (char c : value) {
        if (c < '0' || c > '9') return fallback;
        parsed = parsed * 10 + (c - '0');
    }
    return parsed > 0 ? parsed : fallback;
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

bool ReadOds(const std::string& path, Spreadsheet& out, std::string& error) {
    std::string zip;
    if (!ReadWholeFile(path, zip)) {
        error = "Не удалось прочитать файл: " + path;
        return false;
    }

    size_t cdOffset = 0, cdSize = 0;
    if (!FindCentralDirectory(zip, cdOffset, cdSize)) {
        error = "Не найден центральный каталог ZIP (файл повреждён или не является ODS)";
        return false;
    }

    std::vector<ZipEntry> entries = ReadEntries(zip, cdOffset, cdSize);
    const ZipEntry* content = nullptr;
    for (const ZipEntry& e : entries) {
        if (e.name == "content.xml") {
            content = &e;
            break;
        }
    }
    if (!content) {
        error = "В ODS отсутствует content.xml";
        return false;
    }

    std::string xml;
    if (!ExtractEntry(zip, *content, xml)) {
        error = "Не удалось распаковать content.xml";
        return false;
    }

    out.source = path;
    out.format = "ODS";
    out.rows.clear();

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
        error = "В ODS не найдено ни одной строки";
        return false;
    }
    return true;
}
