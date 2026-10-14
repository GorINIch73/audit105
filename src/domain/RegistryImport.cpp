#include "RegistryImport.h"
#include "Utf8.h"
#include "XlsxReader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const std::vector<std::string> kAltNameAliases = {
    "торговое наименование", "товарное наименование", "торговое название",
    "товарное название", "brand name", "trade name", "торг наименование"};

const std::vector<std::string> kNameAliases = {
    "мнн", "международное непатентованное наименование", "пнн",
    "потребительское наименование", "наименование препарата",
    "наименование лекарственного средства", "наименование лс",
    "лекарственное средство", "действующее вещество", "препарат",
    "наименование", "inn", "generic name", "active substance"};

const std::vector<std::string> kGroupAliases = {
    "фтг", "фармакотерапевтическая группа", "фармакологическая группа",
    "фармакологическая подгруппа", "группа", "перечень", "категория",
    "класс", "программа", "направление", "классификация"};

const std::vector<std::string> kScopeAliases = {"scope", "область", "область применения", "уровень"};

const std::vector<std::string> kSourceAliases = {
    "источник", "документ", "основание", "приказ", "нпа", "постановление",
    "source", "source_doc", "документ-источник"};

const std::vector<std::string> kDateAliases = {
    "дата актуальности", "дата актуализации", "дата включения", "дата регистрации",
    "as_of_date", "as of", "актуальность", "дата"};

const std::vector<std::string> kNoteAliases = {
    "примечание", "примечания", "комментарий", "note", "comment", "описание"};

const std::vector<std::string> kAtxAliases = {
    "атх", "atc", "atx", "анатомо-терапевтическо", "атх код", "атх-код"};

const std::vector<std::string> kOkpdAliases = {
    "окпд", "окпд2", "окпд 2", "номенклатура медизделий", "код медизделия",
    "код медицинской изделия", "код продукции"};

const std::vector<std::string> kCodeAliases = {
    "код узла смнн", "код клп", "код узла", "код позиции", "код лекарственного препарата"};

const std::set<std::string> kFormWords = {
    "таблетки", "таблетка", "капсулы", "капсула", "раствор", "растворы", "ампулы",
    "ампула", "флакон", "флаконы", "суспензия", "крем", "мазь", "гель", "капли",
    "капель", "инъекция", "инъекции", "упаковка", "упаковки", "пачка", "спрей",
    "суппозитории", "суппозиторий", "лиофилизат", "концентрат", "таб"};

const std::set<std::string> kUnitWords = {
    "мг", "мл", "мкг", "г", "ед", "ме", "iu", "unit", "units", "шт", "упак",
    "таб", "кап", "капс", "амп", "фл", "млн", "л", "см3"};

const std::set<std::string> kStopWords = {
    "для", "на", "из", "по", "без", "против", "типа", "тип", "типов", "вид",
    "виды", "форма", "формы", "основа", "основы", "содержит", "состав",
    "лечения", "профилактики", "диагностики", "применения", "также", "или",
    "как", "не", "от", "живая", "живые", "жив", "жидкая", "жидкие", "стер"};

int FindColumn(const std::vector<std::string>& headers, const std::vector<std::string>& aliases) {
    for (size_t i = 0; i < headers.size(); ++i) {
        const std::string& h = headers[i];
        if (h.empty()) continue;
        for (const std::string& alias : aliases) {
            if (h == alias || ContainsUtf8(h, alias)) return static_cast<int>(i);
        }
    }
    return -1;
}

int ExactColumn(const std::vector<std::string>& headers, const std::vector<std::string>& names) {
    for (size_t i = 0; i < headers.size(); ++i) {
        for (const std::string& name : names) {
            if (headers[i] == name) return static_cast<int>(i);
        }
    }
    return -1;
}

int HeaderScore(const Row& row) {
    int score = 0;
    for (const Cell& cell : row.cells) {
        std::string h = NormalizeTextUtf8(cell.text);
        if (h.empty()) continue;
        if (FindColumn({h}, kNameAliases) >= 0 || FindColumn({h}, kAltNameAliases) >= 0 ||
            FindColumn({h}, kGroupAliases) >= 0 || FindColumn({h}, kSourceAliases) >= 0 ||
            FindColumn({h}, kDateAliases) >= 0) {
            ++score;
        }
    }
    return score;
}

// ЕСКЛП-выгрузка содержит строку-нумератор («1, 2, 3, …»): она отделяет
// многострочный заголовок от данных.
int NumberingRow(const std::vector<Row>& rows, int limit) {
    for (int i = 0; i < limit; ++i) {
        const Row& row = rows[i];
        int count = 0;
        bool sequential = true;
        for (const Cell& cell : row.cells) {
            std::string text = TrimUtf8(cell.text);
            if (text.empty()) continue;
            size_t dot = text.find('.');
            std::string head = dot == std::string::npos ? text : text.substr(0, dot);
            if (head.empty()) { sequential = false; break; }
            int value = 0;
            for (char c : head) {
                if (!std::isdigit(static_cast<unsigned char>(c))) { sequential = false; break; }
                value = value * 10 + (c - '0');
            }
            if (!sequential) break;
            if (value != count + 1) { sequential = false; break; }
            ++count;
        }
        if (sequential && count >= 3) return i;
    }
    return -1;
}

bool HasHeaderWord(const std::vector<std::string>& headers, const std::string& word) {
    for (const std::string& header : headers) {
        if (ContainsUtf8(header, word)) return true;
    }
    return false;
}

// Колонка, заголовок которой содержит все перечисленные слова.
int ColumnWith(const std::vector<std::string>& headers, const std::vector<std::string>& words,
               const std::vector<std::string>& forbidden = {}) {
    for (size_t i = 0; i < headers.size(); ++i) {
        const std::string& h = headers[i];
        if (h.empty()) continue;
        bool all = true;
        for (const std::string& word : words) {
            if (!ContainsUtf8(h, word)) { all = false; break; }
        }
        if (!all) continue;
        bool bad = false;
        for (const std::string& word : forbidden) {
            if (ContainsUtf8(h, word)) { bad = true; break; }
        }
        if (!bad) return static_cast<int>(i);
    }
    return -1;
}

void ApplyFormatMap(RegistryFormat kind, const std::vector<std::string>& headers,
                    RegistryColumnMap& map) {
    switch (kind) {
        case RegistryFormat::Smnn:
            map.nameCol = ColumnWith(headers, {"мнн"}, {"список", "код"});
            map.codeCol = ColumnWith(headers, {"код узла смнн"});
            map.okpdCol = ColumnWith(headers, {"окпд"});
            map.formCol = ColumnWith(headers, {"лекарственная форма"}, {"дозировок"});
            map.dosageCol = ColumnWith(headers, {"дозировка"});
            map.groupCol = ColumnWith(headers, {"фтг"});
            map.atxCol = ColumnWith(headers, {"атх"}, {"наименование"});
            map.atxNameCol = ColumnWith(headers, {"атх", "наименование"});
            map.jnvlCol = ColumnWith(headers, {"жнвлп"});
            map.dateCol = ColumnWith(headers, {"дата изменения записи"});
            map.klpFileCol = ColumnWith(headers, {"клп"});
            map.noteCol = ColumnWith(headers, {"некорректные данные"});
            map.altNameCol = ColumnWith(headers, {"список нормализованных мнн"});
            break;
        case RegistryFormat::Klp:
            map.codeCol = ColumnWith(headers, {"код узла смнн"});
            if (map.codeCol < 0) map.codeCol = ColumnWith(headers, {"узел смнн"});
            map.nameCol = ColumnWith(headers, {"нормализованное мнн"});
            if (map.nameCol < 0) map.nameCol = ColumnWith(headers, {"мнн"}, {"список", "код"});
            map.altNameCol = ColumnWith(headers, {"торговое наименование"});
            map.formCol = ColumnWith(headers, {"нормализованная лекарственная форма"});
            if (map.formCol < 0) map.formCol = ColumnWith(headers, {"лекарственная форма"}, {"дозировок"});
            map.dosageCol = ColumnWith(headers, {"нормализованная дозировка"});
            if (map.dosageCol < 0) map.dosageCol = ColumnWith(headers, {"дозировка"});
            map.klpCodeCol = ColumnWith(headers, {"код клп"});
            map.sourceCol = ColumnWith(headers, {"регистрационное удостоверение"});
            map.dateCol = ColumnWith(headers, {"дата изменения записи"});
            map.jnvlCol = ColumnWith(headers, {"жнвлп"});
            map.priceCol = ColumnWith(headers, {"предельная отпускная цена"});
            map.noteCol = ColumnWith(headers, {"некорректные данные"});
            break;
        case RegistryFormat::Trade:
            map.altNameCol = ColumnWith(headers, {"торговое наименование"});
            map.nameCol = ColumnWith(headers, {"мнн"}, {"список", "код"});
            map.codeCol = ColumnWith(headers, {"код узла смнн"});
            map.formCol = ColumnWith(headers, {"лекарственная форма"}, {"дозировок"});
            map.dosageCol = ColumnWith(headers, {"дозировка"});
            map.sourceCol = ColumnWith(headers, {"удостоверения"});
            map.atxCol = ColumnWith(headers, {"атх"});
            break;
        case RegistryFormat::Group:
            map.groupCol = ColumnWith(headers, {"группа взаимозаменяемых"});
            map.nameCol = ColumnWith(headers, {"мнн"}, {"список", "код"});
            if (map.nameCol < 0) map.nameCol = ColumnWith(headers, {"наименование"});
            map.codeCol = ColumnWith(headers, {"код узла смнн"});
            map.dateCol = ColumnWith(headers, {"дата изменения записи"});
            break;
        case RegistryFormat::Generic:
        default:
            break;
    }
    if (map.narcoticCol < 0) map.narcoticCol = ColumnWith(headers, {"наркотических"});
}

bool IsLatinScript(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (!std::isalpha(static_cast<unsigned char>(c))) {
            if (c == '-' || c == '\'' || c == '.') continue;
            return false;
        }
    }
    return true;
}

bool LooksLikeDosage(const std::string& token) {
    bool hasDigit = false;
    bool onlyDigits = true;
    for (char c : token) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            hasDigit = true;
        } else {
            onlyDigits = false;
        }
    }
    if (!hasDigit) return false;
    if (onlyDigits) return true;
    static const std::vector<std::string> units = {"мг", "мл", "мкг", "г", "ед", "ме", "iu", "unit", "units", "шт", "упак", "мл"};
    for (const std::string& unit : units) {
        if (ContainsUtf8(token, unit)) return true;
    }
    return ContainsUtf8(token, "%");
}

std::vector<std::string> SplitTokens(const std::string& text) {
    std::vector<std::string> out;
    std::string token;
    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        if (cp == ' ' || cp == '\t' || cp == '\r' || cp == ',' || cp == ';' || cp == '/' ||
            cp == '\n' || cp == '"' || cp == '(' || cp == ')') {
            std::string trimmed = CollapseSpacesUtf8(token);
            if (!trimmed.empty()) out.push_back(trimmed);
            token.clear();
            continue;
        }
        token += Utf8Encode(cp);
    }
    std::string trimmed = CollapseSpacesUtf8(token);
    if (!trimmed.empty()) out.push_back(trimmed);
    return out;
}

bool IsAtxToken(const std::string& token) {
    if (token.size() < 3 || token.size() > 7) return false;
    if (!std::isalpha(static_cast<unsigned char>(token[0]))) return false;
    size_t i = 1;
    int digits = 0;
    while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) {
        ++digits;
        ++i;
    }
    if (digits < 1 || digits > 2) return false;
    int letters = 0;
    while (i < token.size() && std::isalpha(static_cast<unsigned char>(token[i]))) {
        ++letters;
        ++i;
    }
    if (letters > 3) return false;
    int level = 0;
    while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) {
        ++level;
        ++i;
    }
    if (i != token.size()) return false;
    if (level > 0 && level != 2) return false;
    return letters >= 1 || level == 0;
}

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DaysInYear(int year) { return IsLeapYear(year) ? 366 : 365; }

std::string ExcelSerialToIso(double serial) {
    long days = static_cast<long>(serial);
    if (days < 1 || days > 100000) return std::string();
    long offset = days - 1;
    if (days >= 61) --offset;  // Excel counts a non-existent 1900-02-29
    int year = 1900;
    while (offset >= DaysInYear(year)) {
        offset -= DaysInYear(year);
        ++year;
    }
    static const int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 1;
    while (offset >= kMonthDays[month - 1]) {
        offset -= kMonthDays[month - 1];
        ++month;
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, static_cast<int>(offset) + 1);
    return buffer;
}

}  // namespace

std::string TransliterateLatin(const std::string& latin) {
    static const std::pair<const char*, const char*> kMap[] = {
        {"qu", "кв"}, {"th", "т"}, {"ch", "ч"}, {"sh", "ш"}, {"ph", "ф"}, {"ck", "к"},
        {"cl", "кл"}, {"cy", "ци"}, {"x", "кс"}, {"j", "дж"}, {"y", "и"}, {"a", "а"},
        {"b", "б"}, {"c", "ц"}, {"d", "д"}, {"e", "е"}, {"f", "ф"}, {"g", "г"}, {"h", "х"}, {"i", "и"},
        {"k", "к"}, {"l", "л"}, {"m", "м"}, {"n", "н"}, {"o", "о"}, {"p", "п"},
        {"q", "к"}, {"r", "р"}, {"s", "с"}, {"t", "т"}, {"u", "у"}, {"v", "в"},
        {"w", "в"}, {"z", "з"}};

    std::string lower = ToLowerUtf8(latin);
    std::string out;
    out.reserve(lower.size());
    size_t index = 0;
    while (index < lower.size()) {
        bool matched = false;
        for (const auto& pair : kMap) {
            size_t len = std::strlen(pair.first);
            if (lower.compare(index, len, pair.first) == 0) {
                out += pair.second;
                index += len;
                matched = true;
                break;
            }
        }
        if (!matched) {
            int cp = Utf8Decode(lower, index);
            out += Utf8Encode(cp);
        }
    }
    return out;
}

std::string NormalizeRegistryDate(const std::string& raw) {
    std::string text = CollapseSpacesUtf8(raw);
    if (text.empty()) return std::string();

    std::vector<int> numbers;
    std::string current;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current.push_back(c);
        } else if (!current.empty()) {
            numbers.push_back(std::stoi(current));
            current.clear();
        }
    }
    if (!current.empty()) numbers.push_back(std::stoi(current));
    if (numbers.empty()) return std::string();

    if (numbers.size() == 1) {
        int value = numbers[0];
        if (value >= 10000000 && value <= 99999999) {
            int year = value / 10000;
            int month = (value / 100) % 100;
            int day = value % 100;
            if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
                return buffer;
            }
        }
        std::string serial = ExcelSerialToIso(static_cast<double>(value));
        if (!serial.empty()) return serial;
        return std::string();
    }

    if (numbers.size() == 2 && numbers[1] == 0) {
        return ExcelSerialToIso(static_cast<double>(numbers[0]));
    }

    if (numbers.size() == 3) {
        int a = numbers[0], b = numbers[1], c = numbers[2];
        int year = 0, month = 0, day = 0;
        if (a > 1900 && a <= 2100) {
            year = a; month = b; day = c;
        } else {
            day = a; month = b; year = c;
        }
        if (year < 1900 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
            return std::string();
        }
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
        return buffer;
    }
    return std::string();
}

std::vector<std::string> RegistryNameVariants(const std::string& raw) {
    std::vector<std::string> out;
    for (const std::string& token : SplitTokens(raw)) {
        std::string norm = NormalizeTextUtf8(token);
        if (norm.size() < 3) continue;
        if (LooksLikeDosage(norm)) continue;
        if (kUnitWords.count(norm) > 0) continue;
        if (kFormWords.count(norm) > 0) continue;
        if (kStopWords.count(norm) > 0) continue;
        if (out.empty() || !EqualsUtf8Insensitive(out.back(), norm)) out.push_back(norm);
        if (IsLatinScript(token)) {
            std::string cyrillic = NormalizeTextUtf8(TransliterateLatin(token));
            if (cyrillic.size() >= 3 && !EqualsUtf8Insensitive(cyrillic, norm)) {
                out.push_back(cyrillic);
            }
        }
    }
    return out;
}

std::vector<std::string> ParseAtxCodes(const std::string& raw) {
    std::vector<std::string> out;
    for (const std::string& token : SplitTokens(raw)) {
        std::string code = ToUpperUtf8(CollapseSpacesUtf8(token));
        if (!IsAtxToken(code)) continue;
        bool seen = false;
        for (const std::string& existing : out) {
            if (existing == code) { seen = true; break; }
        }
        if (!seen) out.push_back(code);
    }
    return out;
}

RegistryFormat DetectRegistryFormat(const std::string& sheetName,
                                    const std::vector<std::string>& headers) {
    if (HasHeaderWord(headers, "группа взаимозаменяемых")) return RegistryFormat::Group;
    if (HasHeaderWord(headers, "код клп") || HasHeaderWord(headers, "предельная отпускная цена")) {
        return RegistryFormat::Klp;
    }
    if (HasHeaderWord(headers, "атх") && HasHeaderWord(headers, "код узла смнн")) {
        return RegistryFormat::Smnn;
    }
    if (HasHeaderWord(headers, "торговое наименование") && HasHeaderWord(headers, "код узла смнн")) {
        return RegistryFormat::Trade;
    }
    std::string sheet = ToLowerUtf8(sheetName);
    if (ContainsUtf8(sheet, "klp")) return RegistryFormat::Klp;
    if (ContainsUtf8(sheet, "tn_smnn") || ContainsUtf8(sheet, "торг")) return RegistryFormat::Trade;
    if (ContainsUtf8(sheet, "smnn")) return RegistryFormat::Smnn;
    return RegistryFormat::Generic;
}

std::string RegistryFormatName(RegistryFormat format) {
    switch (format) {
        case RegistryFormat::Smnn: return "ЕСКЛП · СМНН";
        case RegistryFormat::Klp: return "ЕСКЛП · КЛП";
        case RegistryFormat::Trade: return "ЕСКЛП · торговые наименования";
        case RegistryFormat::Group: return "ЕСКЛП · группы взаимозаменяемых ЛП";
        default: return "справочник общего вида";
    }
}

std::vector<ControlFilter> DefaultControlFilters() {
    std::vector<ControlFilter> filters;

    ControlFilter art;
    art.code = "ART";
    art.title = "АРТ при ВИЧ/СПИД (АТХ J05AF, J05AG, J05AR, J05AJ)";
    art.ruleCode = "OMS02";
    art.atxPrefixes = {"J05AF", "J05AG", "J05AR", "J05AJ"};
    art.excludeAtx = {"J05AB", "J05AC", "J05AD", "J05AE"};
    art.ftgMasks = {"антиретровирус", "противовирусное [вич]", "вич"};
    art.excludeMasks = {"герпес", "гепатит"};
    filters.push_back(art);

    ControlFilter tb;
    tb.code = "TB";
    tb.title = "Противотуберкулёзные препараты (АТХ J04A)";
    tb.ruleCode = "OMS03";
    tb.atxPrefixes = {"J04A"};
    tb.ftgMasks = {"противотуберкул"};
    filters.push_back(tb);

    ControlFilter vacc;
    vacc.code = "VACC";
    vacc.title = "Вакцины и иммунопрепараты (АТХ J07)";
    vacc.ruleCode = "OMS01";
    vacc.atxPrefixes = {"J07"};
    vacc.ftgMasks = {"вакцин"};
    filters.push_back(vacc);

    ControlFilter test;
    test.code = "TEST";
    test.title = "Тест-системы на наркотики и алкоголь (ОКПД2, маски имён)";
    test.ruleCode = "OMS05";
    test.okpdPrefixes = {"21.20.23.110", "26.60.12.119"};
    test.nameMasks = {"наркотическ", "опиат", "каннабиноид", "алкотест", "этанол", "хти"};
    test.excludeMasks = {"антисепти", "дезинфек", "гомеопат", "спирт", "раствор для инфузий"};
    test.masksForDevicesOnly = true;
    filters.push_back(test);

    return filters;
}

// Текстовые маски применимы только к позициям, которые не являются лекарственным
// средством: без кода АТХ и без фармакотерапевтической группы (медизделия,
// тест-системы, расходники).
bool LooksLikeDevice(const RegistryEntry& entry) {
    return entry.atx.empty() && entry.group.empty();
}

int MatchLevel(const RegistryEntry& entry, const ControlFilter& filter) {
    auto excluded = [&](const RegistryEntry& e) {
        for (const std::string& mask : filter.excludeMasks) {
            if (ContainsUtf8(e.rawName, mask) || ContainsUtf8(e.name, mask) ||
                ContainsUtf8(e.group, mask) || ContainsUtf8(e.atxGroup, mask)) {
                return true;
            }
            for (const std::string& alt : e.altNames) {
                if (ContainsUtf8(alt, mask)) return true;
            }
        }
        return false;
    };

    for (const std::string& prefix : filter.atxPrefixes) {
        for (const std::string& code : entry.atx) {
            if (code.size() >= prefix.size() &&
                ToLowerUtf8(code).compare(0, prefix.size(), ToLowerUtf8(prefix)) == 0) {
                bool dropped = false;
                for (const std::string& ex : filter.excludeAtx) {
                    if (ContainsUtf8(code, ex)) { dropped = true; break; }
                }
                if (!dropped && !excluded(entry)) return 3;
            }
        }
    }
    for (const std::string& prefix : filter.okpdPrefixes) {
        if (ContainsUtf8(entry.okpd, prefix) && !excluded(entry)) return 3;
    }
    for (const std::string& mask : filter.ftgMasks) {
        if (ContainsUtf8(entry.group, mask) || ContainsUtf8(entry.atxGroup, mask)) {
            if (!excluded(entry)) return 2;
        }
    }
    if (!filter.masksForDevicesOnly || LooksLikeDevice(entry)) {
        for (const std::string& mask : filter.nameMasks) {
            if (excluded(entry)) continue;
            if (ContainsUtf8(entry.name, mask) || ContainsUtf8(entry.rawName, mask)) return 1;
            for (const std::string& alt : entry.altNames) {
                if (ContainsUtf8(alt, mask)) return 1;
            }
        }
    }
    return 0;
}

bool MatchesControlFilter(const RegistryEntry& entry, const ControlFilter& filter) {
    return MatchLevel(entry, filter) > 0;
}

std::string ConfidenceLabel(int level) {
    switch (level) {
        case 3: return "АТХ/ОКПД2";
        case 2: return "ФТГ";
        case 1: return "маска";
        default: return "—";
    }
}

int LinkEntriesByNodeCode(const RegistryProfile& source, RegistryProfile& target) {
    std::unordered_map<std::string, const RegistryEntry*> byCode;
    byCode.reserve(source.entries.size() * 2);
    for (const RegistryEntry& entry : source.entries) {
        if (entry.code.empty()) continue;
        byCode.emplace(CollapseSpacesUtf8(ToLowerUtf8(entry.code)), &entry);
    }

    int linked = 0;
    for (RegistryEntry& entry : target.entries) {
        if (entry.code.empty()) continue;
        auto it = byCode.find(CollapseSpacesUtf8(ToLowerUtf8(entry.code)));
        if (it == byCode.end()) continue;
        const RegistryEntry& src = *it->second;
        if (entry.atx.empty()) entry.atx = src.atx;
        if (entry.group.empty()) entry.group = src.group;
        if (entry.atxGroup.empty()) entry.atxGroup = src.atxGroup;
        if (entry.okpd.empty()) entry.okpd = src.okpd;
        if (entry.jnvl.empty()) entry.jnvl = src.jnvl;
        if (entry.narcotic.empty()) entry.narcotic = src.narcotic;
        ++linked;
    }
    return linked;
}

bool BuildHeaderInfo(const std::vector<Row>& rows, const std::string& sheetName,
                     const std::string& format, RegistryProfile& profile, std::string& error) {
    if (rows.empty()) {
        error = "Файл не содержит строк";
        return false;
    }

    profile.sheetName = sheetName;
    profile.format = format;

    int limit = std::min(12, static_cast<int>(rows.size()));
    int numberingRow = NumberingRow(rows, limit);

    int headerEnd = -1;
    if (numberingRow > 0) {
        headerEnd = numberingRow - 1;
    } else {
        int bestRow = -1;
        int bestScore = 0;
        for (int i = 0; i < limit; ++i) {
            int score = HeaderScore(rows[i]);
            if (score > bestScore) {
                bestScore = score;
                bestRow = i;
            }
        }
        if (bestScore < 2) {
            profile.headerRow = 0;
            profile.dataStartRow = 1;
            profile.warning = "Заголовки не распознаны: термины берутся из первой колонки";
            profile.map.nameCol = 0;
            for (int i = 0; i < rows[0].width; ++i) {
                profile.headers.push_back(rows[0].TextAt(i));
            }
        } else {
            headerEnd = bestRow;
        }
    }

    if (headerEnd >= 0) {
        profile.headerRow = headerEnd;
        profile.dataStartRow = headerEnd + 1;
        if (numberingRow > 0) profile.dataStartRow = numberingRow + 1;

        int width = 0;
        for (int i = 0; i <= headerEnd; ++i) {
            width = std::max(width, rows[i].width);
        }
        for (int col = 0; col < width; ++col) {
            std::string header;
            for (int i = 0; i <= headerEnd; ++i) {
                std::string part = NormalizeTextUtf8(rows[i].TextAt(col));
                if (part.empty()) continue;
                if (header.empty()) header = part;
                else if (!ContainsUtf8(header, part)) header += " " + part;
            }
            profile.headers.push_back(header);
        }

        profile.kind = DetectRegistryFormat(profile.sheetName, profile.headers);
        profile.kindName = RegistryFormatName(profile.kind);
        if (profile.kind != RegistryFormat::Generic) {
            ApplyFormatMap(profile.kind, profile.headers, profile.map);
        }
        if (profile.map.altNameCol < 0) profile.map.altNameCol = FindColumn(profile.headers, kAltNameAliases);
        if (profile.map.groupCol < 0) profile.map.groupCol = FindColumn(profile.headers, kGroupAliases);
        if (profile.map.nameCol < 0) profile.map.nameCol = FindColumn(profile.headers, kNameAliases);
        if (profile.map.scopeCol < 0) profile.map.scopeCol = FindColumn(profile.headers, kScopeAliases);
        if (profile.map.sourceCol < 0) profile.map.sourceCol = FindColumn(profile.headers, kSourceAliases);
        if (profile.map.dateCol < 0) profile.map.dateCol = FindColumn(profile.headers, kDateAliases);
        if (profile.map.noteCol < 0) profile.map.noteCol = FindColumn(profile.headers, kNoteAliases);
        if (profile.map.atxCol < 0) profile.map.atxCol = FindColumn(profile.headers, kAtxAliases);
        if (profile.map.okpdCol < 0) profile.map.okpdCol = FindColumn(profile.headers, kOkpdAliases);
        if (profile.map.codeCol < 0) profile.map.codeCol = FindColumn(profile.headers, kCodeAliases);
        if (profile.kind != RegistryFormat::Generic && profile.map.nameCol < 0) {
            profile.warning = "В схеме " + profile.kindName + " не найдена колонка МНН — применены эвристики";
        }
        if (profile.map.nameCol < 0) profile.map.nameCol = profile.map.altNameCol;
    }

    if (profile.map.nameCol < 0) {
        error = "Не найдена колонка с наименованием (МНН)";
        return false;
    }
    return true;
}

bool EntryFromRow(const Row& row, const RegistryProfile& profile, RegistryEntry& entry) {
    std::string name = TrimUtf8(row.TextAt(profile.map.nameCol));
    if (name.empty() && profile.map.altNameCol >= 0) {
        name = TrimUtf8(row.TextAt(profile.map.altNameCol));
    }
    std::vector<std::string> variants = RegistryNameVariants(name);
    if (variants.empty()) return false;

    entry.rowNo = row.index;
    entry.name = variants.front();
    entry.rawName = CollapseSpacesUtf8(TrimUtf8(name));
    if (profile.map.altNameCol >= 0 && profile.map.altNameCol != profile.map.nameCol) {
        std::vector<std::string> alt = RegistryNameVariants(TrimUtf8(row.TextAt(profile.map.altNameCol)));
        for (const std::string& value : alt) {
            if (!EqualsUtf8Insensitive(value, entry.name)) entry.altNames.push_back(value);
        }
    }
    if (profile.map.groupCol >= 0) {
        entry.group = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.groupCol)));
    }
    if (profile.map.scopeCol >= 0) {
        std::string scope = NormalizeTextUtf8(row.TextAt(profile.map.scopeCol));
        if (!scope.empty()) entry.scope = scope;
    }
    if (profile.map.sourceCol >= 0) {
        entry.sourceDoc = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.sourceCol)));
    }
    if (profile.map.dateCol >= 0) {
        entry.asOfDate = NormalizeRegistryDate(row.TextAt(profile.map.dateCol));
    }
    if (profile.map.noteCol >= 0) {
        entry.note = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.noteCol)));
    }
    if (profile.map.atxCol >= 0) {
        entry.atx = ParseAtxCodes(row.TextAt(profile.map.atxCol));
    }
    if (profile.map.atxNameCol >= 0) {
        entry.atxGroup = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.atxNameCol)));
    }
    if (profile.map.okpdCol >= 0) {
        entry.okpd = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.okpdCol)));
    }
    if (profile.map.codeCol >= 0) {
        entry.code = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.codeCol)));
    }
    if (profile.map.klpCodeCol >= 0) {
        entry.klpCode = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.klpCodeCol)));
    }
    if (profile.map.formCol >= 0) {
        entry.form = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.formCol)));
    }
    if (profile.map.dosageCol >= 0) {
        entry.dosage = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.dosageCol)));
    }
    if (profile.map.jnvlCol >= 0) {
        entry.jnvl = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.jnvlCol)));
    }
    if (profile.map.narcoticCol >= 0) {
        entry.narcotic = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.narcoticCol)));
    }
    if (profile.map.priceCol >= 0) {
        entry.price = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.priceCol)));
    }
    if (profile.map.klpFileCol >= 0) {
        entry.klpFile = CollapseSpacesUtf8(TrimUtf8(row.TextAt(profile.map.klpFileCol)));
    }
    return true;
}

bool BuildRegistryProfile(const Spreadsheet& sheet, RegistryProfile& profile, std::string& error,
                          ScanProgress* progress) {
    profile = RegistryProfile();
    if (!BuildHeaderInfo(sheet.rows, sheet.sheetName, sheet.format, profile, error)) return false;

    if (progress) progress->totalRows.store(sheet.RowCount(), std::memory_order_relaxed);

    for (int i = profile.dataStartRow; i < sheet.RowCount(); ++i) {
        if (progress) {
            progress->rows.fetch_add(1, std::memory_order_relaxed);
            if (progress->cancel.load()) {
                error = "Проход остановлен — отменено пользователем";
                return false;
            }
        }
        RegistryEntry entry;
        if (EntryFromRow(sheet.rows[i], profile, entry)) {
            profile.entries.push_back(std::move(entry));
            if (progress) progress->matches.fetch_add(1, std::memory_order_relaxed);
        } else {
            ++profile.skipped;
        }
    }
    profile.dataRows = sheet.RowCount() - profile.dataStartRow;

    if (profile.entries.empty()) {
        error = "В файле не найдено ни одного термина";
        return false;
    }
    return true;
}

std::unordered_map<std::string, RegistryNodeInfo> BuildNodeIndex(const RegistryProfile& smnn) {
    std::unordered_map<std::string, RegistryNodeInfo> index;
    index.reserve(smnn.entries.size() * 2);
    for (const RegistryEntry& entry : smnn.entries) {
        if (entry.code.empty()) continue;
        RegistryNodeInfo info;
        info.atx = entry.atx;
        info.group = entry.group;
        info.atxGroup = entry.atxGroup;
        info.okpd = entry.okpd;
        info.jnvl = entry.jnvl;
        info.narcotic = entry.narcotic;
        index.emplace(CollapseSpacesUtf8(ToLowerUtf8(entry.code)), std::move(info));
    }
    return index;
}

bool ScanRegistryFile(const std::string& path, const std::string& sheetName,
                      const std::vector<ControlFilter>& filters,
                      const std::unordered_map<std::string, RegistryNodeInfo>& nodes,
                      RegistryProfile& profile, std::string& error,
                      ScanProgress* progress) {
    profile = RegistryProfile();
    profile.matches.assign(filters.size(), {});

    std::vector<Row> head;
    bool headerBuilt = false;
    int position = 0;

    auto process = [&](const Row& row) {
        if (progress) progress->rows.fetch_add(1, std::memory_order_relaxed);
        if (position < profile.dataStartRow) {
            ++position;
            return;
        }
        ++position;
        ++profile.dataRows;

        RegistryEntry entry;
        if (!EntryFromRow(row, profile, entry)) {
            ++profile.skipped;
            return;
        }
        if (!entry.code.empty()) {
            auto it = nodes.find(CollapseSpacesUtf8(ToLowerUtf8(entry.code)));
            if (it != nodes.end()) {
                const RegistryNodeInfo& node = it->second;
                if (entry.atx.empty()) entry.atx = node.atx;
                if (entry.group.empty()) entry.group = node.group;
                if (entry.atxGroup.empty()) entry.atxGroup = node.atxGroup;
                if (entry.okpd.empty()) entry.okpd = node.okpd;
                if (entry.jnvl.empty()) entry.jnvl = node.jnvl;
                if (entry.narcotic.empty()) entry.narcotic = node.narcotic;
                ++profile.linked;
            }
        }

        std::vector<int> levels(filters.size(), 0);
        int best = 0;
        for (size_t f = 0; f < filters.size(); ++f) {
            if (!filters[f].enabled) continue;
            levels[f] = MatchLevel(entry, filters[f]);
            if (levels[f] > best) best = levels[f];
        }
        if (best == 0) return;
        if (progress) progress->matches.fetch_add(1, std::memory_order_relaxed);

        entry.matchLevel = best;
        for (size_t f = 0; f < filters.size(); ++f) {
            if (levels[f] > 0) {
                entry.matchWhy = filters[f].code;
                break;
            }
        }
        int index = static_cast<int>(profile.entries.size());
        profile.entries.push_back(std::move(entry));
        for (size_t f = 0; f < filters.size(); ++f) {
            if (levels[f] > 0) profile.matches[f].push_back(index);
        }
    };

    bool failed = false;
    int totalRows = 0;
    auto visitor = [&](const Row& row) -> bool {
        if (failed) return false;
        if (progress && totalRows > 0) {
            progress->totalRows.store(totalRows, std::memory_order_relaxed);
        }
        if (progress && progress->cancel.load()) {
            failed = true;
            error = "Проход остановлен — отменено пользователем";
            return false;
        }
        if (!headerBuilt) {
            head.push_back(row);
            if (head.size() >= 12) {
                if (!BuildHeaderInfo(head, sheetName, "XLSX", profile, error)) {
                    failed = true;
                    return false;
                }
                headerBuilt = true;
                for (const Row& buffered : head) process(buffered);
            }
            return true;
        }
        process(row);
        return true;
    };

    if (!StreamXlsxRows(path, sheetName, visitor, error, &totalRows)) return false;
    if (progress && totalRows > 0) progress->totalRows.store(totalRows);

    if (!headerBuilt) {
        if (!BuildHeaderInfo(head, sheetName, "XLSX", profile, error)) return false;
        headerBuilt = true;
        for (const Row& buffered : head) process(buffered);
    }

    profile.matchedOnly = true;
    if (profile.entries.empty()) {
        error = "В файле не найдено ни одного совпадения с фильтрами";
        return false;
    }
    return true;
}
