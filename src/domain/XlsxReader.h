#pragma once

#include "Spreadsheet.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct XlsxSheet {
    std::string name;
    std::string entry;
    uint64_t size = 0;
};

bool ListXlsxSheets(const std::string& path, std::vector<XlsxSheet>& sheets, std::string& error);
bool ReadXlsx(const std::string& path, Spreadsheet& out, std::string& error,
              const std::string& sheetName = std::string());

// Проходит лист построчно, распаковывая XML chunk'ами: листы в сотни тысяч строк
// не материализуются в памяти. Visitor возвращает false, чтобы остановить проход.
// totalRowsOut (если задан) получает размер листа из <dimension> — для прогресса.
bool StreamXlsxRows(const std::string& path, const std::string& sheetName,
                    const std::function<bool(const Row&)>& visitor, std::string& error,
                    int* totalRowsOut = nullptr);
