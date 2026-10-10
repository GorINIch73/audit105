#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zip {

struct Entry {
    std::string name;
    uint32_t method = 0;
    uint64_t compSize = 0;
    uint64_t uncompSize = 0;
    uint64_t localOffset = 0;
};

bool ReadWholeFile(const std::string& path, std::string& out);
bool FindCentralDirectory(const std::string& data, size_t& cdOffset, size_t& cdSize);
std::vector<Entry> ReadEntries(const std::string& data, size_t cdOffset, size_t cdSize);
bool ExtractEntry(const std::string& data, const Entry& entry, std::string& out);
const Entry* FindEntry(const std::vector<Entry>& entries, const std::string& name);

}  // namespace zip
