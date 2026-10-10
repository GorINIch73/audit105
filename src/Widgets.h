#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <string>
#include <vector>

bool InputTextSync(const char* label, std::string& value, float width = 0.0f);

float FitItemWidth(const char* label);

bool ComboWithFilter(const char* label, const std::vector<std::string>& options,
                     int& selected, std::string& filter, const ImVec4* accent = nullptr);

bool ComboEnum(const char* label, const std::vector<std::string>& options, std::string& value);
bool IsEnumValue(const std::string& value, const std::vector<std::string>& options);

void ColorSwatch(const ImVec4& color, float size = 14.0f);

void HorizontalSplitter(const char* id, float* firstSize,
                        float minFirst = 60.0f, float maxFirst = -1.0f,
                        float thickness = 8.0f);
void VerticalSplitter(const char* id, float* firstSize,
                      float minFirst = 100.0f, float maxFirst = -1.0f,
                      float thickness = 8.0f, float height = -1.0f);

bool ConfirmationModal(const std::string& id, const std::string& title,
                       const std::string& message,
                       const std::string& confirmLabel = "Подтвердить",
                       const std::string& cancelLabel = "Отмена");

bool AmountInput(const char* label, double& amount);

ImVec4 ParseHexColor(const std::string& hex);
std::string NormalizeHexColor(const std::string& hex);

bool ColorPalettePicker(const char* id, std::string& hex, float swatchWidth = 70.0f);

std::string FormatAmount(double value);
