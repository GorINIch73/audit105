#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include <string>
#include <vector>

class App;

class BaseView {
public:
    virtual ~BaseView() = default;

    virtual void Render() = 0;
    virtual void RefreshData() {}
    virtual bool ForceSave() { return true; }
    virtual void OnDeactivate() {}

    virtual const char* GetTitle() const { return Title.c_str(); }
    void SetTitle(const std::string& title) { Title = title; }

    void RequestDock() {
        WantsDock = true;
        DockFrames = 3;
    }

    void SetApp(App* app_) { app = app_; }

    bool BeginWindow() {
        if (!IsVisible || !IsOpen) return false;
        ImGuiWindow* existing = ImGui::FindWindowByName(GetTitle());
        if (!existing || !existing->DockNode) {
            ImGui::SetNextWindowSizeConstraints(ImVec2(900.0f, 420.0f), ImVec2(10000.0f, 10000.0f));
        }
        if (!ImGui::Begin(GetTitle(), &IsOpen)) {
            ImGui::End();
            if (!IsOpen) IsVisible = false;
            return false;
        }
        IsFocused = GImGui->NavWindow == ImGui::GetCurrentWindowRead();
        return true;
    }

    void EndWindow() {
        ImGui::End();
    }

    void RequestClose() { IsOpen = false; }
    bool IsClosing() const { return !IsOpen; }

    bool IsVisible = false;
    bool IsOpen = true;
    bool IsFocused = false;
    bool WantsDock = false;
    int DockFrames = 0;

protected:
    std::string Title;
    App* app = nullptr;
};
