#pragma once

#include "Spreadsheet.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

// НСИ: импорт перечней и списков из федеральных справочников (ГРЛС, ЕСКЛП).
// Любая таблица (CSV/ODS/XLSX/XML) сводится к профилю: заголовок, карта колонок
// и нормализованные строки справочника. Для выгрузок ЕСКЛП карта колонок
// берётся из фиксированной схемы, а не из эвристического поиска aliases.

enum class RegistryFormat {
    Generic = 0,
    Smnn,     // esklp_smnn_*.xlsx — узлы СМНН (МНН, АТХ, ФТГ, ОКПД2)
    Klp,      // esklp_klp_*.xlsx — позиции КЛП (торговые наименования, РУ, цена)
    Trade,    // tn_smnn_*.xlsx — торговые наименования
    Group,    // esklp_group_*.xlsx — группы взаимозаменяемых ЛП
};

struct RegistryColumnMap {
    int nameCol = -1;       // МНН / наименование препарата
    int altNameCol = -1;    // торговое (товарное) наименование
    int groupCol = -1;      // ФТГ / группа / перечень
    int scopeCol = -1;      // область применения
    int sourceCol = -1;     // источник (документ, приказ, РУ)
    int dateCol = -1;       // дата актуальности
    int noteCol = -1;      // примечание
    int atxCol = -1;        // код АТХ (WHO ATC)
    int atxNameCol = -1;    // наименование АТХ-группы
    int okpdCol = -1;       // код ОКПД2 / номенклатуры медизделий
    int codeCol = -1;       // код узла СМНН
    int klpCodeCol = -1;    // код позиции КЛП
    int formCol = -1;       // лекарственная форма
    int dosageCol = -1;     // дозировка
    int jnvlCol = -1;       // ЖНВЛП
    int narcoticCol = -1;   // признак наркотических/психотропных
    int priceCol = -1;      // предельная отпускная цена
    int klpFileCol = -1;    // ссылка на файл КЛП для узла СМНН
};

struct RegistryEntry {
    int rowNo = 0;
    std::string name;
    std::string rawName;  // исходное наименование целиком — для текстовых масок
    std::vector<std::string> altNames;
    std::string group;
    std::string scope;
    std::string sourceDoc;
    std::string asOfDate;
    std::string note;
    std::vector<std::string> atx;
    std::string atxGroup;
    std::string okpd;
    std::string code;
    std::string klpCode;
    std::string form;
    std::string dosage;
    std::string jnvl;
    std::string narcotic;
    std::string price;
    std::string klpFile;
    int matchLevel = 0;  // 0 нет, 3 АТХ/ОКПД2, 2 ФТГ, 1 текстовая маска
    std::string matchWhy;
};

// Фильтры федерального контроля: АТХ-группы, коды ОКПД2, ФТГ и текстовые маски.
struct ControlFilter {
    std::string code;
    std::string title;
    std::string ruleCode;
    std::vector<std::string> atxPrefixes;
    std::vector<std::string> excludeAtx;
    std::vector<std::string> okpdPrefixes;
    std::vector<std::string> ftgMasks;
    std::vector<std::string> nameMasks;
    std::vector<std::string> excludeMasks;
    bool masksForDevicesOnly = false;  // маски только для позиций без АТХ (медизделия)
    bool enabled = true;
};

struct RegistryProfile {
    std::string sheetName;
    std::string format;
    RegistryFormat kind = RegistryFormat::Generic;
    std::string kindName;
    int headerRow = 0;
    int dataStartRow = 1;
    std::vector<std::string> headers;
    RegistryColumnMap map;
    std::vector<RegistryEntry> entries;
    int skipped = 0;
    int dataRows = 0;   // всего строк данных (не только совпавших)
    int linked = 0;     // позиций, получивших АТХ/ФТГ из узлов СМНН
    bool matchedOnly = false;  // в entries только совпадения с фильтрами (streaming)
    std::vector<std::vector<int>> matches;  // matches[номер фильтра] → индекс в entries
    std::string warning;
};

std::string NormalizeRegistryDate(const std::string& raw);
std::vector<std::string> RegistryNameVariants(const std::string& raw);
std::string TransliterateLatin(const std::string& latin);
std::vector<std::string> ParseAtxCodes(const std::string& raw);

RegistryFormat DetectRegistryFormat(const std::string& sheetName,
                                    const std::vector<std::string>& headers);
std::string RegistryFormatName(RegistryFormat format);

std::vector<ControlFilter> DefaultControlFilters();
int MatchLevel(const RegistryEntry& entry, const ControlFilter& filter);
bool MatchesControlFilter(const RegistryEntry& entry, const ControlFilter& filter);
std::string ConfidenceLabel(int level);

// Позиции КЛП не содержат АТХ/ОКПД2: подтягиваем их по коду узла СМНН.
int LinkEntriesByNodeCode(const RegistryProfile& source, RegistryProfile& target);

// Индекс узлов СМНН по коду — нужен, чтобы связывать позиции КЛП со справочником
// без хранения всего профиля КЛП в памяти.
struct RegistryNodeInfo {
    std::vector<std::string> atx;
    std::string group;
    std::string atxGroup;
    std::string okpd;
    std::string jnvl;
    std::string narcotic;
};
std::unordered_map<std::string, RegistryNodeInfo> BuildNodeIndex(const RegistryProfile& smnn);

bool BuildHeaderInfo(const std::vector<Row>& rows, const std::string& sheetName,
                     const std::string& format, RegistryProfile& profile, std::string& error);
bool EntryFromRow(const Row& row, const RegistryProfile& profile, RegistryEntry& entry);

// Счётчики прогресса чтения справочника — читаются UI каждый кадр.
struct ScanProgress {
    std::atomic<int> rows{0};
    std::atomic<int> matches{0};
    std::atomic<int> totalRows{0};
    std::atomic<bool> cancel{false};
};

// Построчный проход большого листа ЕСКЛП: в profile.entries попадают только
// совпадения с фильтрами, остальное остаётся в счётчиках.
bool ScanRegistryFile(const std::string& path, const std::string& sheetName,
                      const std::vector<ControlFilter>& filters,
                      const std::unordered_map<std::string, RegistryNodeInfo>& nodes,
                      RegistryProfile& profile, std::string& error,
                      ScanProgress* progress = nullptr);

bool BuildRegistryProfile(const Spreadsheet& sheet, RegistryProfile& profile, std::string& error,
                          ScanProgress* progress = nullptr);
