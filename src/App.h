#pragma once

#include "imgui.h"
#include <memory>
#include <string>
#include <vector>

#include "Database.h"
#include "views/BaseView.h"

struct GLFWwindow;

class App {
public:
    App();
    ~App();

    void SetWindow(GLFWwindow* window);
    void SetMainDockId(ImGuiID dockId);

    void RenderMenuBar();
    void RenderViews();
    bool HasError() const { return !errorMessage.empty(); }

    template <typename T>
    T* CreateView(const std::string& title = "") {
        auto view = std::make_unique<T>();
        T* ptr = view.get();
        ptr->SetApp(this);
        std::string t = title.empty() ? ptr->GetTitle() : title;
        if (t.empty()) t = "Форма " + std::to_string(viewIdCounter);
        ptr->SetTitle(t + "###view" + std::to_string(viewIdCounter++));
        ptr->IsVisible = true;
        ptr->RequestDock();
        views.push_back(std::move(view));
        return ptr;
    }

    bool OpenDatabase(const std::string& path);
    bool SaveDatabaseAs(const std::string& path);
    bool BackupDatabase(const std::string& reason);
    Database& Db() { return db; }
    bool HasDatabase() const { return db.IsOpen(); }

    bool SaveAllViews();
    void RefreshAllViews();
    void CloseAllViews();

    void ApplyTheme(int index);
    void ApplyFont(int size);
    void LoadFonts();

    void SetDbPath(const std::string& path);
    const std::string& DbPath() const { return dbPath; }
    void AddRecentDb(const std::string& path);

    void SetStatus(const std::string& text);
    void ShowError(const std::string& message);

    std::vector<std::unique_ptr<BaseView>> views;
    std::vector<std::string> recentDbPaths;
    std::string refsPath = "refs";

    bool showAbout = false;
    bool isImporting = false;

private:
    void RenderStatusBar();
    void RenderErrorPopup();

    GLFWwindow* window = nullptr;
    ImGuiID mainDockId = 0;
    Database db;
    int themeIndex = 0;
    int fontSize = 22;
    int appliedFontSize = 0;
    bool fontsLoaded = false;
    std::string dbPath;
    std::string statusLine;
    std::string errorMessage;
    int viewIdCounter = 0;
};
