#pragma once

#include "Spreadsheet.h"

#include <string>

bool ReadCsv(const std::string& path, Spreadsheet& out, std::string& error);
