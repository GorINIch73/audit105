#include "FormatDetector.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace {

std::string LowerExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

std::string ReadHead(const std::string& path, size_t bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::string head(bytes, '\0');
    in.read(&head[0], static_cast<std::streamsize>(bytes));
    head.resize(static_cast<size_t>(in.gcount()));
    return head;
}

}  // namespace

InputFormat DetectFormat(const std::string& path) {
    const std::string head = ReadHead(path, 8);
    const std::string ext = LowerExt(path);

    if (head.size() >= 4 && head[0] == 'P' && head[1] == 'K' && head[2] == '\x03' && head[3] == '\x04') {
        if (ext == "xlsx" || ext == "xlsm") return InputFormat::Xlsx;
        if (ext == "ods") return InputFormat::Ods;
        return InputFormat::Ods;
    }
    if (head.size() >= 4 && head[0] == '\xD0' && head[1] == '\xCF' && head[2] == '\x11' && head[3] == '\xE0') {
        return InputFormat::Xls;
    }
    if (!head.empty() && (head[0] == '<' || (head.size() >= 5 && head.compare(0, 5, "<?xml") == 0))) {
        return InputFormat::Xml;
    }
    if (ext == "ods") return InputFormat::Ods;
    if (ext == "xlsx" || ext == "xlsm") return InputFormat::Xlsx;
    if (ext == "xls") return InputFormat::Xls;
    if (ext == "xml") return InputFormat::Xml;
    if (ext == "csv" || ext == "tsv") return InputFormat::Csv;
    return InputFormat::Unknown;
}

const char* FormatName(InputFormat format) {
    switch (format) {
        case InputFormat::Ods: return "ODS";
        case InputFormat::Xlsx: return "XLSX";
        case InputFormat::Xml: return "XML";
        case InputFormat::Xls: return "XLS";
        case InputFormat::Csv: return "CSV";
        default: return "неизвестен";
    }
}

bool NeedsConversion(InputFormat format) {
    return format == InputFormat::Xls;
}
