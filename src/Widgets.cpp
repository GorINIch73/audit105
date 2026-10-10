#include "Widgets.h"
#include "domain/Utf8.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

bool InputTextSync(const char* label, std::string& value, float width) {
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    return ImGui::InputText(label, &value);
}

float FitItemWidth(const char* label) {
    float avail = ImGui::GetContentRegionAvail().x;
    float labelWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemInnerSpacing.x;
    return ImMax(40.0f, avail - labelWidth);
}

std::string FormatAmount(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}

bool AmountInput(const char* label, double& amount) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", amount);
    if (ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_CharsDecimal)) {
        amount = std::strtod(buf, nullptr);
        return true;
    }
    return false;
}

static void DrawSplitter(const char* id, float* firstSize, float minFirst,
                         float maxFirst, bool horizontal, float thickness, float secondSize) {
    ImGui::InvisibleButton(id, horizontal ? ImVec2(-1.0f, thickness)
                                          : ImVec2(thickness, secondSize));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    ImU32 color = ImGui::GetColorU32(
        ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);

    if (horizontal) {
        float center = (min.y + max.y) / 2.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(min.x, center - 1.0f), ImVec2(max.x, center + 1.0f), color);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) *firstSize += ImGui::GetIO().MouseDelta.y;
    } else {
        float center = (min.x + max.x) / 2.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(center - 1.0f, min.y), ImVec2(center + 1.0f, max.y), color);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) *firstSize += ImGui::GetIO().MouseDelta.x;
    }

    if (*firstSize < minFirst) *firstSize = minFirst;
    if (maxFirst > 0.0f && *firstSize > maxFirst) *firstSize = maxFirst;
}

void HorizontalSplitter(const char* id, float* firstSize, float minFirst,
                        float maxFirst, float thickness) {
    DrawSplitter(id, firstSize, minFirst, maxFirst, true, thickness, -1.0f);
}

void VerticalSplitter(const char* id, float* firstSize, float minFirst,
                      float maxFirst, float thickness, float height) {
    DrawSplitter(id, firstSize, minFirst, maxFirst, false, thickness, height);
}

ImVec4 ParseHexColor(const std::string& hex) {
    int r = 255, g = 255, b = 255;
    if (hex.size() >= 7 && hex[0] == '#') {
        r = std::stoi(hex.substr(1, 2), nullptr, 16);
        g = std::stoi(hex.substr(3, 2), nullptr, 16);
        b = std::stoi(hex.substr(5, 2), nullptr, 16);
    }
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

std::string NormalizeHexColor(const std::string& hex) {
    std::string value;
    for (char ch : hex) {
        if (ch == '#' || std::isspace(static_cast<unsigned char>(ch))) continue;
        value.push_back(ch);
    }
    if (value.size() == 3) {
        value = std::string{value[0], value[0], value[1], value[1], value[2], value[2]};
    }
    if (value.size() != 6) return std::string();
    for (char ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return std::string();
    }
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return "#" + value;
}

static ImVec4 SwatchTextColor(const ImVec4& color) {
    float luminance = 0.299f * color.x + 0.587f * color.y + 0.114f * color.z;
    return luminance > 0.62f ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void ColorSwatch(const ImVec4& color, float size) {
    ImGui::Dummy(ImVec2(size, size));
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImU32 fill = ImGui::GetColorU32(color);
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, fill);
    ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));
}

bool ColorPalettePicker(const char* id, std::string& hex, float swatchWidth) {
    static const char* kPalette[][2] = {
        {"Красный", "#e74c3c"},        {"Тёмно-красный", "#c0392b"},
        {"Оранжевый", "#e67e22"},      {"Жёлтый", "#f4c430"},
        {"Зелёный", "#2ecc71"},        {"Тёмно-зелёный", "#27ae60"},
        {"Бирюзовый", "#1abc9c"},      {"Голубой", "#3498db"},
        {"Синий", "#2980b9"},          {"Фиолетовый", "#9b59b6"},
        {"Пурпурный", "#8e44ad"},      {"Розовый", "#ff6b81"},
        {"Лаймовый", "#a3e635"},       {"Небесный", "#61a5b7"},
        {"Графит", "#34495e"},         {"Серый", "#7f8c8d"},
        {"Светло-серый", "#bdc3c7"},   {"Коричневый", "#8d5524"},
        {"Бордовый", "#7d3c98"},       {"Тёмно-бирюзовый", "#16a085"},
        {"Охра", "#d35400"},           {"Сиреневый", "#b39ddb"},
        {"Сталь", "#4b777d"},          {"Белый", "#ecf0f1"},
    };

    ImVec4 current = ParseHexColor(hex);

    ImGui::PushStyleColor(ImGuiCol_Button, current);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, current);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, current);
    ImGui::PushStyleColor(ImGuiCol_Text, SwatchTextColor(current));
    ImGui::Button(hex.c_str(), ImVec2(swatchWidth, 0.0f));
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemClicked()) ImGui::OpenPopup(id);

    bool changed = false;
    if (ImGui::BeginPopup(id)) {
        ImGui::Text(ICON_FA_PALETTE "  Палитра цветов");
        ImGui::TextDisabled("Текущий: %s", hex.c_str());
        ImGui::Separator();

        if (ImGui::BeginTable("color_palette", 6, ImGuiTableFlags_Borders)) {
            for (int c = 0; c < 6; ++c) {
                ImGui::TableSetupColumn("swatch", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            }
            for (int i = 0; i < 24; i += 6) {
                ImGui::TableNextRow();
                for (int c = 0; c < 6; ++c) {
                    ImGui::TableNextColumn();
                    const char* name = kPalette[i + c][0];
                    const char* value = kPalette[i + c][1];
                    ImVec4 color = ParseHexColor(value);

                    ImGui::PushID(value);
                    ImGui::PushStyleColor(ImGuiCol_Button, color);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
                    ImGui::PushStyleColor(ImGuiCol_Text, SwatchTextColor(color));
                    ImGui::Button(name, ImVec2(-1.0f, 24.0f));
                    ImGui::PopStyleColor(4);
                    if (ImGui::IsItemClicked()) {
                        hex = value;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
    return changed;
}

bool ComboWithFilter(const char* label, const std::vector<std::string>& options,
                     int& selected, std::string& filter, const ImVec4* accent) {
    bool changed = false;
    std::string popupId = std::string("##combofilter_") + std::to_string(ImGui::GetID(label));
    std::string current = (selected >= 0 && selected < (int)options.size())
                              ? options[static_cast<size_t>(selected)]
                              : std::string("все");

    if (label && label[0] != '\0') {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }
    const float width = ImGui::CalcTextSize(current.c_str()).x + 2.0f * ImGui::GetStyle().FramePadding.x;

    if (accent) ImGui::PushStyleColor(ImGuiCol_Text, *accent);
    if (ImGui::Selectable(current.c_str(), false, ImGuiSelectableFlags_NoPadWithHalfSpacing,
                          ImVec2(width, 0.0f))) {
        ImGui::OpenPopup(popupId.c_str());
    }
    if (accent) ImGui::PopStyleColor();

    if (ImGui::BeginPopup(popupId.c_str())) {
        InputTextSync("Фильтр", filter, 220.0f);
        ImGui::Separator();
        const std::string needle = NormalizeTextUtf8(filter);
        if (options.empty()) ImGui::TextDisabled("(пусто)");
        for (size_t i = 0; i < options.size(); ++i) {
            if (needle.empty() || ContainsUtf8(options[i], needle)) {
                if (ImGui::Selectable(options[i].c_str(), selected == (int)i)) {
                    selected = (int)i;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool IsEnumValue(const std::string& value, const std::vector<std::string>& options) {
    for (const std::string& option : options) {
        if (EqualsUtf8Insensitive(option, value)) return true;
    }
    return false;
}

static const char* EnumGetter(void* data, int idx) {
    return (*reinterpret_cast<std::vector<std::string>*>(data))[static_cast<size_t>(idx)].c_str();
}

bool ComboEnum(const char* label, const std::vector<std::string>& options, std::string& value) {
    if (options.empty()) return false;

    int index = 0;
    for (size_t i = 0; i < options.size(); ++i) {
        if (EqualsUtf8Insensitive(options[i], value)) {
            index = static_cast<int>(i);
            break;
        }
    }

    ImGui::SetNextItemWidth(FitItemWidth(label));
    if (ImGui::Combo(label, &index, EnumGetter,
                     const_cast<std::vector<std::string>*>(&options),
                     static_cast<int>(options.size()))) {
        value = options[static_cast<size_t>(index)];
        return true;
    }

    if (!IsEnumValue(value, options)) {
        std::string allowed;
        for (size_t i = 0; i < options.size(); ++i) {
            if (i > 0) allowed += ", ";
            allowed += options[i];
        }
        ImGui::TextDisabled("%s  «%s» — не из списка. Допустимо: %s",
                            ICON_FA_CIRCLE_EXCLAMATION, value.c_str(), allowed.c_str());
    }
    return false;
}

bool ConfirmationModal(const std::string& id, const std::string& title,
                       const std::string& message,
                       const std::string& confirmLabel,
                       const std::string& cancelLabel) {
    bool confirmed = false;
    if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Dummy(ImVec2(320.0f, 0.0f));
        ImGui::Text(ICON_FA_CIRCLE_EXCLAMATION "  %s", title.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.30f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.38f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.24f, 0.22f, 1.0f));
        if (ImGui::Button(confirmLabel.c_str())) {
            confirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button(cancelLabel.c_str())) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
    return confirmed;
}
