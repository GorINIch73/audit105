#pragma once

#include "Spreadsheet.h"

#include <string>

bool ReadOds(const std::string& path, Spreadsheet& out, std::string& error);
