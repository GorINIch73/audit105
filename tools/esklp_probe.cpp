#include "../src/domain/FormatDetector.h"
#include "../src/domain/RegistryImport.h"
#include "../src/domain/Spreadsheet.h"
#include "../src/domain/XlsxReader.h"
#include "../src/domain/Utf8.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string path = "esklp/esklp_smnn_20261014.xlsx";
    std::string sheetName;
    bool stream = false;
    std::string nodeFile;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--stream") stream = true;
        else if (a == "--nodes") nodeFile = argv[i + 1];
        else if (a == "--sheet") sheetName = argv[i + 1];
        else if (path == "esklp/esklp_smnn_20261014.xlsx") path = a;
    }

    std::vector<XlsxSheet> sheets;
    std::string error;
    if (!ListXlsxSheets(path, sheets, error)) {
        std::cout << "list error: " << error << "\n";
        return 1;
    }
    for (const XlsxSheet& s : sheets) std::cout << "sheet: " << s.name << " size=" << s.size << "\n";

    if (sheetName.empty()) {
        const XlsxSheet* best = &sheets[0];
        for (const XlsxSheet& s : sheets) if (s.size > best->size) best = &s;
        sheetName = best->name;
    }

    RegistryProfile profile;
    if (stream) {
        std::unordered_map<std::string, RegistryNodeInfo> nodes;
        if (!nodeFile.empty()) {
            Spreadsheet nodeSheet;
            if (!ReadXlsx(nodeFile, nodeSheet, error, "")) {
                std::cout << "node read error: " << error << "\n";
                return 1;
            }
            RegistryProfile nodeProfile;
            if (!BuildRegistryProfile(nodeSheet, nodeProfile, error)) {
                std::cout << "node profile error: " << error << "\n";
                return 1;
            }
            nodes = BuildNodeIndex(nodeProfile);
            std::cout << "node index: " << nodes.size() << " SMNN nodes\n";
        }
        if (!ScanRegistryFile(path, sheetName, DefaultControlFilters(), nodes, profile, error)) {
            std::cout << "scan error: " << error << "\n";
            return 1;
        }
    } else {
        Spreadsheet sheet;
        if (!ReadXlsx(path, sheet, error, sheetName)) {
            std::cout << "read error: " << error << "\n";
            return 1;
        }
        if (!BuildRegistryProfile(sheet, profile, error)) {
            std::cout << "profile error: " << error << "\n";
            return 1;
        }
    }

    std::cout << "dataRows=" << profile.dataRows << " entries=" << profile.entries.size()
              << " skipped=" << profile.skipped << " linked=" << profile.linked
              << " warning=" << profile.warning << "\n";
    std::cout << "kind=" << RegistryFormatName(profile.kind)
              << " headerRow=" << profile.headerRow << " dataStart=" << profile.dataStartRow << "\n";
    std::cout << "headers:\n";
    for (size_t i = 0; i < profile.headers.size(); ++i) std::cout << "  " << i << ": " << profile.headers[i] << "\n";

    std::cout << "map name=" << profile.map.nameCol << " alt=" << profile.map.altNameCol
              << " group=" << profile.map.groupCol << " atx=" << profile.map.atxCol
              << " okpd=" << profile.map.okpdCol << " code=" << profile.map.codeCol
              << " date=" << profile.map.dateCol << " atxName=" << profile.map.atxNameCol
              << " form=" << profile.map.formCol << " dosage=" << profile.map.dosageCol
              << " jnvl=" << profile.map.jnvlCol << " price=" << profile.map.priceCol
              << " klpFile=" << profile.map.klpFileCol << "\n";

    int withAtx = 0, withOkpd = 0;
    for (const RegistryEntry& e : profile.entries) {
        if (!e.atx.empty()) ++withAtx;
        if (!e.okpd.empty()) ++withOkpd;
    }
    std::cout << "entries with ATX=" << withAtx << " with OKPD2=" << withOkpd << "\n";

    std::map<std::string,int> pref;
    for (const RegistryEntry& e : profile.entries)
        for (const std::string& c : e.atx) pref[c.substr(0,4)]++;
    for (const auto& kv : pref)
        if (kv.first.compare(0,3,"J05")==0 || kv.first.compare(0,3,"J04")==0 || kv.first.compare(0,3,"J07")==0)
            std::cout << "atx " << kv.first << " = " << kv.second << "\n";

    for (const ControlFilter& f : DefaultControlFilters()) {
        int count = 0, strong = 0, ftg = 0, mask = 0;
        for (const RegistryEntry& e : profile.entries) {
            int level = MatchLevel(e, f);
            if (level == 0) continue;
            ++count;
            if (level == 3) ++strong;
            else if (level == 2) ++ftg;
            else ++mask;
        }
        std::cout << "filter " << f.code << " (" << f.ruleCode << ") -> " << count
                  << " [АТХ/ОКПД2=" << strong << " ФТГ=" << ftg << " маска=" << mask << "]\n";
    }

    for (const ControlFilter& f : DefaultControlFilters()) {
        std::cout << "--- " << f.code << "\n";
        int shown = 0;
        for (const RegistryEntry& e : profile.entries) {
            if (!MatchesControlFilter(e, f)) continue;
            std::string atx;
            for (const std::string& c : e.atx) { if (!atx.empty()) atx += "; "; atx += c; }
            std::cout << "  " << e.name << " | " << atx << " | " << e.okpd << " | " << e.group
                      << " | lvl=" << MatchLevel(e, f) << "\n";
            if (++shown >= 10) break;
        }
    }
    return 0;
}
