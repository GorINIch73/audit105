#include "../src/Database.h"
#include "../src/domain/CsvReader.h"
#include "../src/domain/OdsReader.h"
#include "../src/domain/PositionParser.h"
#include "../src/domain/RegistryImport.h"
#include "../src/domain/Spreadsheet.h"
#include "../src/domain/Utf8.h"
#include "../src/domain/XlsxReader.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

static int failures = 0;

static void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    } else {
        std::cout << "ok: " << what << "\n";
    }
}

static int CountMatches(const RegistryProfile& profile, const ControlFilter& filter) {
    int count = 0;
    for (const RegistryEntry& entry : profile.entries) {
        if (MatchesControlFilter(entry, filter)) ++count;
    }
    return count;
}

static const ControlFilter* FindFilter(const std::vector<ControlFilter>& filters,
                                       const std::string& code) {
    for (const ControlFilter& filter : filters) {
        if (filter.code == code) return &filter;
    }
    return nullptr;
}

static Spreadsheet MakeSheet(const std::string& name,
                             const std::vector<std::vector<std::string>>& rows) {
    Spreadsheet sheet;
    sheet.sheetName = name;
    sheet.format = "XLSX";
    for (size_t i = 0; i < rows.size(); ++i) {
        Row row;
        row.index = static_cast<int>(i);
        row.width = static_cast<int>(rows[i].size());
        for (size_t c = 0; c < rows[i].size(); ++c) {
            Cell cell;
            cell.col = static_cast<int>(c);
            cell.text = rows[i][c];
            row.cells.push_back(cell);
        }
        sheet.rows.push_back(row);
    }
    return sheet;
}

int main() {
    Check(ParseAtxCodes("L04AX;\nJ05AX").size() == 2, "ATX codes split from a multi-line cell");
    Check(ParseAtxCodes("J05AX") == std::vector<std::string>{"J05AX"}, "single ATX code parsed");
    Check(ParseAtxCodes("21.20.10.194-000129-1-00001").empty(), "OKPD2 code is not taken as ATX");
    Check(ParseAtxCodes("ТАБЛЕТКИ").empty(), "text is not taken as ATX");

    Check(NormalizeRegistryDate("14.10.2026") == "2026-10-14", "dd.mm.yyyy normalized");
    Check(NormalizeRegistryDate("2026-10-14") == "2026-10-14", "ISO date kept");
    Check(NormalizeRegistryDate("46115.0") == "2026-04-03", "Excel serial with .0 normalized");
    Check(NormalizeRegistryDate("20261014") == "2026-10-14", "compact yyyymmdd normalized");
    Check(NormalizeRegistryDate("").empty(), "empty date stays empty");

    std::vector<std::string> variants = RegistryNameVariants("ЗИДОВУДИН 300 МГ ТАБЛЕТКИ");
    Check(variants.size() == 1 && variants[0] == "зидовудин", "dosage and form words dropped from INN");
    std::vector<std::string> latin = RegistryNameVariants("ACYCLOVIR");
    Check(latin.size() >= 2 && latin[1] == "ацикловир", "latin INN gets a transliterated variant");

    ControlFilter art;
    art.code = "ART";
    art.atxPrefixes = {"J05AF"};
    RegistryEntry entry;
    entry.atx = {"J05AF01"};
    Check(MatchesControlFilter(entry, art), "ATX prefix matches");
    entry.atx = {"J05AB"};
    Check(!MatchesControlFilter(entry, art), "non-matching ATX prefix rejected");
    ControlFilter withExclude;
    withExclude.atxPrefixes = {"J05A"};
    withExclude.excludeAtx = {"J05AB"};
    entry.atx = {"J05AB01"};
    Check(!MatchesControlFilter(entry, withExclude), "excluded ATX branch rejected");
    ControlFilter maskFilter;
    maskFilter.nameMasks = {"наркотическ"};
    entry.name = "Тест-система";
    entry.altNames = {"набор наркотический скрининговый"};
    Check(MatchesControlFilter(entry, maskFilter), "name mask matches an alternative name");

    const std::string fixture = "tests/fixtures/esklp_smnn_sample.xlsx";
    std::string error;
    std::vector<XlsxSheet> sheets;
    if (!ListXlsxSheets(fixture, sheets, error)) {
        std::cerr << "fixture read failed: " << error << "\n";
        return 1;
    }
    Check(sheets.size() == 1, "fixture has one sheet");

    Spreadsheet sheet;
    if (!ReadXlsx(fixture, sheet, error)) {
        std::cerr << "fixture read failed: " << error << "\n";
        return 1;
    }

    RegistryProfile profile;
    if (!BuildRegistryProfile(sheet, profile, error)) {
        std::cerr << "profile build failed: " << error << "\n";
        return 1;
    }

    Check(profile.map.nameCol == 0, "SMNN column for standardized INN detected");
    Check(profile.map.codeCol == 1, "SMNN node code column detected");
    Check(profile.map.okpdCol == 2, "OKPD2 column detected");
    Check(profile.map.atxCol == 13, "ATX column detected");
    Check(profile.map.groupCol == 12, "pharmacotherapeutic group column detected");
    Check(profile.kind == RegistryFormat::Smnn, "ESKLP SMNN schema detected");
    Check(profile.dataStartRow == 4, "ESKLP numbering row separates header from data");
    Check(profile.map.atxNameCol == 14, "ATX group name column detected");
    Check(profile.map.formCol == 3, "dosage form column detected");
    Check(profile.map.jnvlCol == 15, "JNVL column detected");
    Check(profile.map.klpFileCol == 35, "KLP file link column detected");
    Check(profile.entries.size() > 200, "profile parsed more than 200 SMNN nodes");
    Check(profile.skipped >= 0, "skipped rows counted");

    int withAtx = 0, withOkpd = 0, withDate = 0;
    for (const RegistryEntry& e : profile.entries) {
        if (!e.atx.empty()) ++withAtx;
        if (!e.okpd.empty()) ++withOkpd;
        if (!e.asOfDate.empty()) ++withDate;
    }
    Check(withAtx > 20, "ATX codes present in the sample");
    Check(withOkpd > 100, "OKPD2 codes present in the sample");
    Check(withDate > 100, "registry dates normalized from Excel serials");

    std::vector<ControlFilter> filters = DefaultControlFilters();
    Check(filters.size() == 4, "four default control filters");
    Check(CountMatches(profile, *FindFilter(filters, "ART")) > 0, "ART filter finds matches");
    Check(CountMatches(profile, *FindFilter(filters, "TB")) > 0, "TB filter finds matches");
    Check(CountMatches(profile, *FindFilter(filters, "VACC")) > 0, "VACC filter finds matches");
    Check(CountMatches(profile, *FindFilter(filters, "TEST")) == 0,
          "TEST filter does not pick drug nodes (masks are device-only)");

    {
        Spreadsheet devices = MakeSheet("mi_test_systems", {
            {"Наименование", "ОКПД2", "Примечание"},
            {"1", "2", "3"},
            {"Набор ХТИ «Наркотест»", "26.60.12.119", ""},
            {"Этанол антисептический", "21.20.10.194", ""},
        });
        RegistryProfile deviceProfile;
        Check(BuildRegistryProfile(devices, deviceProfile, error), "medical device profile built");
        Check(deviceProfile.kind == RegistryFormat::Generic, "unknown registry keeps the generic schema");
        Check(deviceProfile.dataStartRow == 2, "device data starts after the numbering row");
        Check(CountMatches(deviceProfile, *FindFilter(filters, "TEST")) == 1,
              "TEST filter keeps the test system and drops the antiseptic");
    }

    bool artExcludesHerpes = false;
    for (const RegistryEntry& e : profile.entries) {
        for (const std::string& code : e.atx) {
            if (ContainsUtf8(code, "J05AB") && MatchesControlFilter(e, *FindFilter(filters, "ART"))) {
                artExcludesHerpes = true;
            }
        }
    }
    Check(!artExcludesHerpes, "ART filter does not pick up the excluded ATX branch");

    {
        Spreadsheet klp = MakeSheet("esklp_klp_20261014_00000", {
            {"Узел СМНН", "", "", "Код КЛП", "Торговое наименование", "Нормализованное МНН",
             "Нормализованная лекарственная форма", "Нормализованная дозировка", "ЖНВЛП",
             "Наличие в лекарственном препарате наркотических средств", "Период действия КЛП",
             "Дата изменения записи", "Предельная отпускная цена на позицию КЛП", "Некорректные данные"},
            {"Код узла СМНН", "Стандартизованное МНН", "Стандартизованная лекарственная форма и дозировка",
             "", "", "", "", "", "", "", "", "", "", ""},
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14"},
            {"21.20.10.194-000129-1-00001-0000000000000", "1-[2-(1-МЕТИЛИМИДАЗОЛ-4-ИЛ)-ЭТИЛ]ПЕРГИДРОАЗИН-2,6-ДИОН",
             "ТАБЛЕТКИ, ПОКРЫТЫЕ ОБОЛОЧКОЙ (100 мг)", "21.20.10.194-000129-1-00001-2000001905712",
             "Атериксен", "Пергидрозин", "ТАБЛЕТКИ ПОКРЫТЫЕ ПЛЕНОЧНОЙ ОБОЛОЧКОЙ", "100 мг", "Да", "Нет",
             "46113.0", "46210.0", "189.50", ""},
            {"21.20.10.194-000129-1-00002-0000000000000", "ЗИДОВУДИН", "ТАБЛЕТКИ (300 мг)",
             "21.20.10.194-000129-1-00002-2000001905713", "Виривин", "Зидовудин", "ТАБЛЕТКИ", "300 мг",
             "Да", "Нет", "46113.0", "46210.0", "210.00", ""},
        });
        RegistryProfile klpProfile;
        Check(BuildRegistryProfile(klp, klpProfile, error), "KLP profile built");
        Check(klpProfile.kind == RegistryFormat::Klp, "KLP schema detected");
        Check(klpProfile.dataStartRow == 3, "KLP data starts after the numbering row");
        Check(klpProfile.map.nameCol == 5, "KLP normalized INN column detected");
        Check(klpProfile.map.altNameCol == 4, "KLP trade name column detected");
        Check(klpProfile.map.klpCodeCol == 3, "KLP position code column detected");
        Check(klpProfile.map.priceCol == 12, "KLP price column detected");
        Check(klpProfile.entries.size() == 2, "KLP rows parsed");
        Check(klpProfile.entries[0].code == "21.20.10.194-000129-1-00001-0000000000000",
             "KLP SMNN node code kept");
        Check(!klpProfile.entries[0].altNames.empty(), "KLP trade name kept as an alternative name");
        Check(klpProfile.entries[0].atx.empty(), "KLP positions have no ATX of their own");

        int linked = LinkEntriesByNodeCode(profile, klpProfile);
        Check(linked >= 1, "KLP positions linked to SMNN nodes by node code");
        Check(!klpProfile.entries[0].atx.empty(), "linked KLP position inherits ATX from SMNN");
        Check(!klpProfile.entries[0].group.empty(), "linked KLP position inherits the FTG group");
        Check(MatchLevel(klpProfile.entries[1], *FindFilter(filters, "ART")) == 0,
             "unlinked KLP position is not picked by the ART filter");
        ControlFilter linkedFilter;
        linkedFilter.code = "LINK";
        linkedFilter.title = "Связанная позиция";
        linkedFilter.ruleCode = "OMS02";
        if (!klpProfile.entries[0].atx.empty()) {
            linkedFilter.atxPrefixes = {klpProfile.entries[0].atx.front().substr(0, 4)};
        }
        Check(MatchLevel(klpProfile.entries[0], linkedFilter) == 3,
             "KLP position becomes filterable after linking to the SMNN node");
    }

    {
        const std::string klpFixture = "tests/fixtures/esklp_klp_sample.xlsx";
        std::unordered_map<std::string, RegistryNodeInfo> nodes = BuildNodeIndex(profile);
        std::vector<ControlFilter> defaults = DefaultControlFilters();
        RegistryProfile streamed;
        Check(ScanRegistryFile(klpFixture, "", defaults, nodes, streamed, error),
              "streaming KLP scan succeeds");
        Check(streamed.kind == RegistryFormat::Klp, "streaming scan detects the KLP schema");
        Check(streamed.dataStartRow == 2, "streaming data starts after the numbering row");
        Check(streamed.dataRows == 4, "streaming counted every KLP data row");
        Check(streamed.entries.size() == 2, "streaming materializes only filter matches");
        Check(streamed.linked == 3, "streaming linked KLP positions to SMNN nodes");
        Check(streamed.matchedOnly, "streaming profile keeps only matched entries");
        Check(streamed.matches[0].size() == 2, "streaming ART filter found two matches");
        bool inheritedAtx = false;
        for (const RegistryEntry& e : streamed.entries) {
            for (const std::string& code : e.atx) {
                if (code.compare(0, 4, "J05A") == 0) inheritedAtx = true;
            }
        }
        Check(inheritedAtx, "streaming KLP position inherits ATX from the SMNN node");
    }

    {
        const std::string klpFixture = "tests/fixtures/esklp_klp_sample.xlsx";
        std::unordered_map<std::string, RegistryNodeInfo> nodes = BuildNodeIndex(profile);
        std::vector<ControlFilter> defaults = DefaultControlFilters();

        ScanProgress progress;
        RegistryProfile streamed;
        Check(ScanRegistryFile(klpFixture, "", defaults, nodes, streamed, error, &progress),
              "scan with progress counters succeeds");
        Check(progress.rows.load() >= streamed.dataRows, "progress counted the scanned rows");
        Check(progress.matches.load() == static_cast<int>(streamed.entries.size()),
              "progress counted the matches");

        ScanProgress cancelled;
        cancelled.cancel.store(true);
        RegistryProfile aborted;
        std::string cancelError;
        Check(!ScanRegistryFile(klpFixture, "", defaults, nodes, aborted, cancelError, &cancelled),
              "scan stops when cancellation is requested");
        Check(cancelError.find("остановлен") != std::string::npos,
              "cancellation reports a clear reason");
    }

    {
        ControlFilter art;
        art.atxPrefixes = {"J05AF"};
        art.ftgMasks = {"антиретровирус"};
        RegistryEntry strong;
        strong.atx = {"J05AF01"};
        Check(MatchLevel(strong, art) == 3, "ATX hit is a strong match");
        RegistryEntry ftg;
        ftg.name = "невир";
        ftg.group = "антиретровирусные препараты";
        Check(MatchLevel(ftg, art) == 2, "FTG hit is a weaker match");
        ControlFilter device;
        device.nameMasks = {"хти"};
        device.masksForDevicesOnly = true;
        RegistryEntry drug;
        drug.name = "Этанол для инъекций";
        drug.atx = {"A01AX"};
        Check(MatchLevel(drug, device) == 0, "name masks are skipped for drug entries");
        RegistryEntry deviceEntry;
        deviceEntry.name = "набор хти";
        Check(MatchLevel(deviceEntry, device) == 1, "name mask matches a device entry");
        ControlFilter withExclude;
        withExclude.nameMasks = {"этанол"};
        withExclude.excludeMasks = {"антисепти"};
        RegistryEntry antiseptic;
        antiseptic.name = "этанол антисептический";
        Check(MatchLevel(antiseptic, withExclude) == 0, "excluded mask does not match an antiseptic");
        RegistryEntry homeopathic;
        homeopathic.name = "placenta";
        homeopathic.rawName = "Placenta ethanol";
        homeopathic.group = "гомеопатическое средство";
        ControlFilter homeo;
        homeo.nameMasks = {"этанол"};
        homeo.excludeMasks = {"гомеопат"};
        homeo.masksForDevicesOnly = true;
        Check(MatchLevel(homeopathic, homeo) == 0, "exclusion works on the group, not only the name");
        Check(ConfidenceLabel(2) == "ФТГ", "confidence label for FTG hits");
    }

    const std::string csvFixture = "/tmp/audit105_registry_sample.csv";
    {
        std::ofstream out(csvFixture);
        out << "МНН;Торговое наименование;ФТГ;АТХ;Дата актуальности\n";
        out << "Зидовудин;Виривин;антиретровирусные средства;J05AR01;12.05.2025\n";
        out << "Ингибитор протеазы;Протеиназа;;J05AE02;01.01.2024\n";
        out.close();
    }
    Spreadsheet csvSheet;
    if (ReadCsv(csvFixture, csvSheet, error)) {
        RegistryProfile csvProfile;
        Check(BuildRegistryProfile(csvSheet, csvProfile, error), "CSV registry profile built");
        Check(csvProfile.map.nameCol == 0 && csvProfile.map.atxCol == 3, "CSV columns detected");
        Check(csvProfile.entries.size() == 2, "CSV rows parsed");
        Check(csvProfile.entries[0].asOfDate == "2025-05-12", "CSV date normalized");
    } else {
        Check(false, "CSV fixture read: " + error);
    }

    const std::string dbPath = "/tmp/audit105_registry_test.db";
    std::remove(dbPath.c_str());
    std::remove((dbPath + "-wal").c_str());
    std::remove((dbPath + "-shm").c_str());
    Database db;
    if (!db.Open(dbPath)) {
        std::cerr << "database open failed\n";
        return 1;
    }

    int64_t oms02 = 0;
    for (const Rule& rule : db.ListRules()) {
        if (rule.code == "OMS02") oms02 = rule.id;
    }
    Check(oms02 != 0, "seeded rule OMS02 available for import");

    std::vector<RegistryEntry> artEntries;
    for (const RegistryEntry& e : profile.entries) {
        if (MatchesControlFilter(e, *FindFilter(filters, "ART"))) {
            RegistryEntry copy = e;
            copy.matchLevel = MatchLevel(e, *FindFilter(filters, "ART"));
            artEntries.push_back(copy);
        }
    }

    RegistrySource source;
    source.name = "esklp_smnn_sample.xlsx";
    source.file = fixture;
    source.ruleCode = "OMS02";
    source.listName = "АРТ при ВИЧ/СПИД (АТХ J05AF, J05AG, J05AR, J05AJ)";
    source.format = "XLSX";
    source.asOfDate = "2026-10-14";
    Term seededManualTermObj;
    seededManualTermObj.ruleId = oms02;
    seededManualTermObj.term = "Зидовудин";
    seededManualTermObj.scope = "global";
    seededManualTermObj.origin = "manual";
    seededManualTermObj.listName = source.listName;
    db.AddTerm(seededManualTermObj);

    int64_t sourceId = db.RegisterRegistrySource(source);
    Check(sourceId != 0, "registry source registered");

    RegistryImportStats stats;
    Check(db.ImportRegistryEntries(oms02, source.listName, artEntries, sourceId,
                                   source.name, source.asOfDate, stats),
          "ART entries imported");
    Check(stats.entries == static_cast<int>(artEntries.size()), "import processed every entry");
    Check(stats.added > 0, "import added new terms");

    int importedTerms = 0;
    bool hasOrigin = false, hasSource = false, hasDate = false, hasConfidence = false;
    for (const Term& term : db.ListTerms()) {
        if (term.listName != source.listName) continue;
        ++importedTerms;
        if (term.origin == "import") hasOrigin = true;
        if (!term.sourceDoc.empty()) hasSource = true;
        if (!term.asOfDate.empty()) hasDate = true;
        if (term.confidence >= 3) hasConfidence = true;
    }
    Check(importedTerms > 0, "imported terms are stored in the list");
    Check(hasOrigin, "imported terms marked with origin=import");
    Check(hasSource, "imported terms keep the source document");
    Check(hasDate, "imported terms keep the registry date");
    Check(hasConfidence, "imported terms keep the ATX match confidence");
    bool manualHasConfidence = false;
    for (const Term& term : db.ListTerms()) {
        if (term.origin == "manual" && term.confidence > 0) manualHasConfidence = true;
    }
    Check(!manualHasConfidence, "manual terms have no import confidence");

    RegistryImportStats again;
    Check(db.ImportRegistryEntries(oms02, source.listName, artEntries, sourceId,
                                   source.name, source.asOfDate, again),
          "second import of the same registry succeeds");
    Check(again.added == 0, "re-import does not duplicate terms");
    Check(again.updated > 0, "re-import updates existing terms");

    bool seededManualTerm = false;
    for (const Term& term : db.ListTerms()) {
        if (term.termNorm == "зидовудин" && term.origin == "manual") seededManualTerm = true;
    }
    Check(seededManualTerm, "seeded manual term still present");

    std::vector<RegistrySource> sources = db.ListRegistrySources();
    Check(sources.size() == 1, "one registry source registered");
    Check(sources[0].added > 0, "registry source stores the number of added terms");

    int removed = 0;
    Check(db.DeleteRegistrySource(sourceId, removed), "registry source deleted");
    Check(removed > 0, "deleting the source removes its imported terms");
    Check(db.ListRegistrySources().empty(), "no registry sources left after delete");

    {
        Spreadsheet ods;
        std::string odsError;
        if (!ReadOds("tests/fixtures/105. 2026.ods", ods, odsError)) {
            Check(false, "reference ODS readable: " + odsError);
        } else {
            ParseReport report = ParsePositions(ods);
            Run run;
            run.file = "105. 2026.ods";
            run.format = "ods";
            run.organization = "Регрессия ЕСКЛП";
            run.period = "2026";
            int64_t runId = db.ImportRun(run, report);
            Check(runId != 0, "reference run imported for regression");

            int flaggedBefore = db.ApplyRules(runId);
            int omsBefore = 0;
            for (const Flag& flag : db.ListRunFlags(runId)) {
                if (flag.ruleCode.rfind("OMS", 0) == 0) ++omsBefore;
            }

            int importedLists = 0;
            for (const ControlFilter& filter : filters) {
                if (!filter.enabled) continue;
                int64_t ruleId = 0;
                for (const Rule& rule : db.ListRules()) {
                    if (EqualsUtf8Insensitive(rule.code, filter.ruleCode)) {
                        ruleId = rule.id;
                        break;
                    }
                }
                if (ruleId == 0) continue;

                std::vector<RegistryEntry> selected;
                for (const RegistryEntry& entry : profile.entries) {
                    if (MatchesControlFilter(entry, filter)) selected.push_back(entry);
                }
                if (selected.empty()) continue;

                RegistrySource reg;
                reg.name = "esklp_smnn_sample.xlsx";
                reg.file = fixture;
                reg.ruleCode = filter.ruleCode;
                reg.listName = filter.title;
                reg.format = "XLSX";
                reg.asOfDate = "2026-10-14";
                int64_t regId = db.RegisterRegistrySource(reg);
                RegistryImportStats regStats;
                if (db.ImportRegistryEntries(ruleId, reg.listName, selected, regId,
                                            reg.name, reg.asOfDate, regStats)) {
                    ++importedLists;
                }
            }

            int flaggedAfter = db.ApplyRules(runId);
            Check(importedLists > 0, "regression import processed several filters");
            Check(flaggedAfter >= flaggedBefore,
                 "ESKLP import does not reduce flagged positions");

            std::unordered_map<int64_t, std::string> itemNames;
            for (const ItemRecord& record : db.ListRunItems(runId)) {
                itemNames[record.id] = ToLowerUtf8(record.item.nameNorm);
            }

            const std::vector<std::string> nonDrugWords = {
                "перчатк", "шприц", "катетер", "бинт", "марля", "инфузион", "система"};
            int falsePositives = 0;
            for (const Flag& flag : db.ListRunFlags(runId)) {
                if (flag.ruleCode.rfind("OMS", 0) != 0) continue;
                auto it = itemNames.find(flag.itemId);
                if (it == itemNames.end()) continue;
                for (const std::string& word : nonDrugWords) {
                    if (ContainsUtf8(it->second, word)) {
                        ++falsePositives;
                        std::cout << "  false positive: " << it->second << " / " << flag.ruleCode << "\n";
                        break;
                    }
                }
            }
            Check(falsePositives == 0, "imported ATX lists do not flag non-drug positions");
            std::cout << "regression: flagged " << flaggedBefore << " -> " << flaggedAfter
                      << " (OMS flags " << omsBefore << ")\n";
        }
    }

    std::cout << (failures == 0 ? "OK\n" : "FAILURES: " + std::to_string(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
