#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <string>
#include <vector>

bool InputTextSync(const char* label, std::string& value, float width = 0.0f);

bool ComboWithFilter(const char* label, const std::vector<std::string>& options,
                    int& selected, std::string& filter);

void HorizontalSplitter(const char* id, float* firstSize,
                        float minFirst = 60.0f, float maxFirst = -1.0f,
                        float thickness = 8.0f);
void VerticalSplitter(const char* id, float* firstSize,
                      float minFirst = 100.0f, float maxFirst = -1.0f,
                      float thickness = 8.0f);

bool ConfirmationModal(const std::string& id, const std::string& title,
                       const std::string& message,
                       const std::string& confirmLabel = "Подтвердить",
                       const std::string& cancelLabel = "Отмена");

bool AmountInput(const char* label, double& amount);

std::string FormatAmount(double value);
