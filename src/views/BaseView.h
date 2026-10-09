#pragma once

#include "imgui.h"
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

    bool IsVisible = false;
    bool WantsDock = false;
    int DockFrames = 0;

protected:
    std::string Title;
    App* app = nullptr;
};
