#include "Widgets.h"
#include "domain/Utf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

bool InputTextSync(const char* label, std::string& value, float width) {
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    return ImGui::InputText(label, &value);
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
                         float maxFirst, bool horizontal, float thickness) {
    ImGui::InvisibleButton(id, horizontal ? ImVec2(-1.0f, thickness)
                                          : ImVec2(thickness, -1.0f));

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
    DrawSplitter(id, firstSize, minFirst, maxFirst, true, thickness);
}

void VerticalSplitter(const char* id, float* firstSize, float minFirst,
                      float maxFirst, float thickness) {
    DrawSplitter(id, firstSize, minFirst, maxFirst, false, thickness);
}

bool ComboWithFilter(const char* label, const std::vector<std::string>& options,
                     int& selected, std::string& filter) {
    bool changed = false;
    std::string popupId = std::string("##combofilter_") + label;

    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::Button(selected >= 0 && selected < (int)options.size()
                      ? options[static_cast<size_t>(selected)].c_str()
                      : "Выбрать…");
    ImGui::SameLine();
    if (ImGui::SmallButton("▼")) ImGui::OpenPopup(popupId.c_str());

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

bool ConfirmationModal(const std::string& id, const std::string& title,
                       const std::string& message,
                       const std::string& confirmLabel,
                       const std::string& cancelLabel) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    bool confirmed = false;
    if (ImGui::Begin(id.c_str(), nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoDocking)) {
        ImGui::TextUnformatted(title.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::Separator();
        if (ImGui::Button(confirmLabel.c_str())) confirmed = true;
        ImGui::SameLine();
        if (ImGui::Button(cancelLabel.c_str())) ImGui::CloseCurrentPopup();
        ImGui::End();
    }
    return confirmed;
}
