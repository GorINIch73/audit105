#include "Zip.h"

#include <zlib.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace zip {

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static uint32_t ReadU32(const std::string& data, size_t at) {
    if (at + 4 > data.size()) return 0;
    return static_cast<uint32_t>(
        static_cast<unsigned char>(data[at]) |
        (static_cast<unsigned int>(static_cast<unsigned char>(data[at + 1])) << 8) |
        (static_cast<unsigned int>(static_cast<unsigned char>(data[at + 2])) << 16) |
        (static_cast<unsigned long long>(static_cast<unsigned char>(data[at + 3])) << 24));
}

static uint16_t ReadU16(const std::string& data, size_t at) {
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

std::vector<Entry> ReadEntries(const std::string& data, size_t cdOffset, size_t cdSize) {
    std::vector<Entry> entries;
    size_t pos = cdOffset;
    size_t end = cdOffset + cdSize;
    while (pos + 46 <= end) {
        if (data[pos] != 'P' || data[pos + 1] != 'K' || data[pos + 2] != 0x01 || data[pos + 3] != 0x02) break;
        Entry e;
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

static bool InflateRaw(const std::string& data, size_t from, size_t length, size_t hint, std::string& out) {
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

bool ExtractEntry(const std::string& data, const Entry& entry, std::string& out) {
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

const Entry* FindEntry(const std::vector<Entry>& entries, const std::string& name) {
    for (const Entry& e : entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

}  // namespace zip
