#pragma once

#include <string>

enum class InputFormat {
    Unknown,
    Ods,
    Xlsx,
    Xml,
    Xls,
    Csv,
};

InputFormat DetectFormat(const std::string& path);
const char* FormatName(InputFormat format);
bool NeedsConversion(InputFormat format);
