#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Database.h"
#include "ImGuiFileDialog.h"
#include "views/BaseView.h"

struct GLFWwindow;

enum LogLevel {
    LogInfo = 0,
    LogWarn = 1,
    LogError = 2,
    LogSuccess = 3,
};

struct LogEntry {
    std::string time;
    std::string text;
    int level = LogInfo;
};

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

    void LoadSession();
    void SaveSession();
    std::string LastDb() const;
    void UpdateWindowTitle();

    bool SaveAllViews();
    void RefreshAllViews();
    void CloseAllViews();
    void CloseView(BaseView* view);
    void CloseFocusedView();
    size_t OpenViewCount() const { return views.size(); }

    void ApplyTheme(int index);
    void ApplyFont(int size);
    void LoadFonts();

    void SetDbPath(const std::string& path);
    const std::string& DbPath() const { return dbPath; }
    void AddRecentDb(const std::string& path);

    void SetStatus(const std::string& text);
    void ShowError(const std::string& message);

    void LogEvent(const std::string& text, int level = LogInfo);
    const std::vector<LogEntry>& Events() const { return events; }
    void ClearEvents();

    void SetProgress(const std::string& text, double fraction = -1.0);
    void ClearProgress();
    bool HasProgress() const { return progressActive; }

    void OpenFileDialog(const std::string& key, const std::string& title, const char* filters,
                        const IGFD::FileDialogConfig& config = IGFD::FileDialogConfig{});
    void ClaimDialog(const std::string& key);
    void CloseStaleDialogs(const std::vector<std::string>& extraKeys);

    void MarkRulesChanged();
    int RulesVersion() const { return rulesVersion; }
    int RunRulesVersion(int64_t runId) const;
    bool RulesOutOfDate(int64_t runId) const;
    int ReapplyRules(int64_t runId);
    void MarkRunRulesApplied(int64_t runId);

    std::vector<std::unique_ptr<BaseView>> views;
    std::vector<std::string> recentDbPaths;
    std::string lastDbPath;
    std::string refsPath = "refs";

    bool showAbout = false;
    bool isImporting = false;
    bool showEventLog = false;

private:
    void RenderStatusBar();
    void RenderEventLog();
    void RenderErrorPopup();

    GLFWwindow* window = nullptr;
    ImGuiID mainDockId = 0;
    BaseView* focusedView = nullptr;
    Database db;
    int themeIndex = 0;
    int fontSize = 22;
    int appliedFontSize = 0;
    bool fontsLoaded = false;
    std::string dbPath;
    std::string statusLine;
    std::string errorMessage;
    std::vector<LogEntry> events;
    std::vector<std::string> claimedDialogs;
    std::string progressText;
    double progressValue = -1.0;
    bool progressActive = false;
    std::chrono::steady_clock::time_point progressStart;
    int rulesVersion = 0;
    std::unordered_map<int64_t, int> runRulesVersion;
    int viewIdCounter = 0;
};
