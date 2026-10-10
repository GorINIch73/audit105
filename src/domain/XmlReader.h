#pragma once

#include "Spreadsheet.h"

#include <string>

bool ReadXml(const std::string& path, Spreadsheet& out, std::string& error);
