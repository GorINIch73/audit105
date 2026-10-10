#pragma once

#include "Spreadsheet.h"

#include <string>

bool ReadXlsx(const std::string& path, Spreadsheet& out, std::string& error);
