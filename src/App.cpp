#include "App.h"

#include "imgui_internal.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>

#include "IconsFontAwesome6.h"
#include "ImGuiFileDialog.h"
#include "PlatformUtils.h"
#include "Widgets.h"
#include "views/ImportView.h"
#include "views/FormView.h"
#include "views/RulesView.h"
#include "views/RulesEditorView.h"
#include "views/CategoriesEditorView.h"
#include "views/TermsEditorView.h"
#include "views/ListsEditorView.h"
#include "views/ReportView.h"
#include "views/RunsView.h"
#include "views/CompareView.h"
#include "views/SettingsView.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace {
std::string SessionPath() { return AppDataDir() + "/session.txt"; }
}

App::App() = default;
App::~App() = default;

void App::LoadSession() {
    std::ifstream in(SessionPath());
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.rfind("last=", 0) == 0) {
            lastDbPath = line.substr(5);
        } else if (line.rfind("recent=", 0) == 0) {
            recentDbPaths.push_back(line.substr(7));
        }
    }
    if (lastDbPath.empty() && !recentDbPaths.empty()) lastDbPath = recentDbPaths.front();
}

void App::SaveSession() {
    std::filesystem::create_directories(AppDataDir());
    std::ofstream out(SessionPath());
    if (!out) return;
    out << "last=" << lastDbPath << "\n";
    for (const auto& path : recentDbPaths) out << "recent=" << path << "\n";
}

std::string App::LastDb() const {
    if (!lastDbPath.empty()) return lastDbPath;
    if (!recentDbPaths.empty()) return recentDbPaths.front();
    return DefaultDbPath();
}

void App::UpdateWindowTitle() {
    if (!window) return;
    std::string title = "Аудит счёта 105";
    if (!dbPath.empty()) {
        title += " — " + std::filesystem::path(dbPath).filename().string();
    }
    glfwSetWindowTitle(window, title.c_str());
}

void App::SetWindow(GLFWwindow* w) { window = w; }
void App::SetMainDockId(ImGuiID id) { mainDockId = id; }

bool App::OpenDatabase(const std::string& path) {
    if (path.empty()) return false;
    if (!db.Open(path)) {
        ShowError("Не удалось открыть базу: " + path);
        return false;
    }
    SetDbPath(db.Path());
    AddRecentDb(db.Path());
    UpdateWindowTitle();
    SetStatus("База открыта: " + db.Path());
    rulesVersion = 0;
    runRulesVersion.clear();
    RefreshAllViews();
    return true;
}

bool App::SaveDatabaseAs(const std::string& path) {
    if (!db.IsOpen() || path.empty()) return false;
    if (!db.SaveAs(path)) {
        ShowError("Не удалось сохранить копию базы: " + path);
        return false;
    }
    SetDbPath(db.Path());
    AddRecentDb(db.Path());
    UpdateWindowTitle();
    SetStatus("Копия базы: " + db.Path());
    return true;
}

bool App::BackupDatabase(const std::string& reason) {
    if (!db.IsOpen()) return false;
    std::string out;
    if (!db.Backup(reason, out)) {
        ShowError("Не удалось создать резервную копию");
        return false;
    }
    SetStatus("Резервная копия: " + out);
    return true;
}

void App::SetDbPath(const std::string& path) {
    dbPath = path;
    AddRecentDb(path);
    SetStatus("База: " + path);
}

void App::AddRecentDb(const std::string& path) {
    if (path.empty()) return;
    lastDbPath = path;
    auto it = std::find(recentDbPaths.begin(), recentDbPaths.end(), path);
    if (it != recentDbPaths.end()) recentDbPaths.erase(it);
    recentDbPaths.insert(recentDbPaths.begin(), path);
    if (recentDbPaths.size() > 10) recentDbPaths.resize(10);
    SaveSession();
}

void App::SetStatus(const std::string& text) { statusLine = text; }

void App::MarkRulesChanged() { ++rulesVersion; }

int App::RunRulesVersion(int64_t runId) const {
    auto it = runRulesVersion.find(runId);
    return it == runRulesVersion.end() ? 0 : it->second;
}

bool App::RulesOutOfDate(int64_t runId) const {
    return RunRulesVersion(runId) != rulesVersion;
}

void App::MarkRunRulesApplied(int64_t runId) {
    if (runId > 0) runRulesVersion[runId] = rulesVersion;
}

int App::ReapplyRules(int64_t runId) {
    if (!HasDatabase() || runId <= 0) return 0;
    int flagged = db.ApplyRules(runId);
    runRulesVersion[runId] = rulesVersion;
    for (auto& view : views) view->RefreshData();
    SetStatus("Правила переприменены: отмечено позиций — " + std::to_string(flagged));
    return flagged;
}

void App::ShowError(const std::string& message) { errorMessage = message; }

bool App::SaveAllViews() {
    for (auto& view : views) {
        if (!view->ForceSave()) return false;
    }
    return true;
}

void App::RefreshAllViews() {
    for (auto& view : views) view->RefreshData();
}

void App::CloseAllViews() {
    for (auto& view : views) {
        view->RequestClose();
        view->IsVisible = false;
    }
    views.clear();
    focusedView = nullptr;
    SetStatus("Все формы закрыты");
}

void App::CloseView(BaseView* view) {
    if (!view) return;
    std::string title = view->GetTitle();
    view->OnDeactivate();
    view->RequestClose();
    view->IsVisible = false;
    if (focusedView == view) focusedView = nullptr;
    SetStatus("Форма закрыта: " + title);
}

void App::CloseFocusedView() {
    if (!focusedView) {
        SetStatus("Нет активной формы (Ctrl+W)");
        return;
    }
    CloseView(focusedView);
}

void App::ApplyTheme(int index) {
    themeIndex = index;
    ImGuiStyle& style = ImGui::GetStyle();
    switch (index) {
        case 1: ImGui::StyleColorsLight(); break;
        case 2: ImGui::StyleColorsClassic(); break;
        case 3: {
            ImGui::StyleColorsDark();
            ImVec4* c = style.Colors;
            c[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.96f, 1.00f);
            c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.12f, 0.16f, 1.00f);
            c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.14f, 0.18f, 1.00f);
            c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.14f, 0.18f, 0.98f);
            c[ImGuiCol_Border] = ImVec4(0.22f, 0.31f, 0.40f, 0.70f);
            c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.20f, 0.26f, 1.00f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.29f, 0.37f, 1.00f);
            c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.36f, 0.46f, 1.00f);
            c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.11f, 0.15f, 1.00f);
            c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.18f, 0.24f, 1.00f);
            c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.14f, 0.18f, 1.00f);
            c[ImGuiCol_Header] = ImVec4(0.24f, 0.38f, 0.45f, 0.85f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.50f, 0.57f, 0.95f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.58f, 0.64f, 1.00f);
            c[ImGuiCol_Button] = ImVec4(0.21f, 0.37f, 0.45f, 0.92f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.50f, 0.58f, 1.00f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.61f, 0.68f, 1.00f);
            c[ImGuiCol_Tab] = ImVec4(0.13f, 0.18f, 0.23f, 1.00f);
            c[ImGuiCol_Separator] = ImVec4(0.70f, 0.50f, 0.00f, 1.00f);
            break;
        }
        default: {
            ImGui::StyleColorsDark();
            ImVec4* c = style.Colors;
            c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
            c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
            c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.15f, 0.98f);
            c[ImGuiCol_Border] = ImVec4(0.30f, 0.33f, 0.36f, 0.65f);
            c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.25f, 0.27f, 1.00f);
            c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);
            c[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
            c[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.20f, 0.22f, 1.00f);
            c[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
            c[ImGuiCol_Header] = ImVec4(0.22f, 0.35f, 0.42f, 0.75f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.45f, 0.52f, 0.85f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.52f, 0.60f, 1.00f);
            c[ImGuiCol_Button] = ImVec4(0.20f, 0.34f, 0.40f, 0.90f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.45f, 0.52f, 1.00f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.31f, 0.53f, 0.61f, 1.00f);
            c[ImGuiCol_Tab] = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
            c[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.45f, 0.52f, 1.00f);
            c[ImGuiCol_TabActive] = ImVec4(0.22f, 0.34f, 0.40f, 1.00f);
            c[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.76f, 0.84f, 1.00f);
            break;
        }
    }
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.AntiAliasedLines = true;
}

void App::LoadFonts() {
    if (fontsLoaded) return;
    ImGuiIO& io = ImGui::GetIO();

    std::string roboto = AssetPath("Roboto-Regular.ttf");
    bool base = false;
    if (!roboto.empty()) {
        ImFontConfig cfg;
        base = io.Fonts->AddFontFromFileTTF(roboto.c_str(), 0.0f, &cfg,
                                           io.Fonts->GetGlyphRangesCyrillic()) != nullptr;
    }
    if (!base) io.Fonts->AddFontDefault();

    std::string fa = AssetPath("fa-solid-900.otf");
    if (!fa.empty()) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
        static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
        io.Fonts->AddFontFromFileTTF(fa.c_str(), 0.0f, &cfg, icon_ranges);
    }
    fontsLoaded = true;
}

void App::ApplyFont(int size) {
    size = std::clamp(size, 14, 40);
    if (appliedFontSize == size) return;
    LoadFonts();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = static_cast<float>(size);
    style._NextFrameFontSizeBase = style.FontSizeBase;
    appliedFontSize = size;
    fontSize = size;
}

void App::RenderMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu(ICON_FA_FILE " Файл")) {
        if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS " Создать новую базу")) {
            if (SaveAllViews()) {
                ImGuiFileDialog::Instance()->OpenDialog("NewDbDlg", "Файл для новой базы", ".db");
            }
        }
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Открыть базу")) {
            if (SaveAllViews()) {
                ImGuiFileDialog::Instance()->OpenDialog("OpenDbDlg", "Файл базы данных", ".db");
            }
        }
        if (!dbPath.empty() && ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Сохранить базу как...")) {
            IGFD::FileDialogConfig config;
            config.filePathName = dbPath;
            ImGuiFileDialog::Instance()->OpenDialog("SaveDbAsDlg", "Сохранить базу как", ".db", config);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT " Недавние")) {
            if (recentDbPaths.empty()) ImGui::TextDisabled("(нет)");
            for (const auto& path : recentDbPaths) {
                if (ImGui::MenuItem(path.c_str())) OpenDatabase(path);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT_FROM_BRACKET " Выход")) {
            if (SaveAllViews()) {
                BackupDatabase("exit");
                if (window) glfwSetWindowShouldClose(window, true);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_UPLOAD " Импорт")) {
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Импорт оборота (ODS/XLSX/XML)")) {
            CreateView<ImportView>("Импорт оборота");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_STETHOSCOPE " Форма МЗ")) {
        if (ImGui::MenuItem(ICON_FA_TABLE " Таблица позиций")) {
            CreateView<FormView>("Форма МЗ");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_GAVEL " Правила")) {
        if (ImGui::MenuItem(ICON_FA_LIST " Правила")) {
            CreateView<RulesEditorView>("Правила");
        }
        if (ImGui::MenuItem(ICON_FA_TAGS " Категории")) {
            CreateView<CategoriesEditorView>("Категории");
        }
        if (ImGui::MenuItem(ICON_FA_BOOK_OPEN " Термины и списки")) {
            CreateView<TermsEditorView>("Термины");
        }
        if (ImGui::MenuItem(ICON_FA_LIST " Перечни")) {
            CreateView<ListsEditorView>("Перечни");
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_CHART_PIE " Сводка и очередь")) {
            CreateView<RulesView>("Сводка правил");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_LIST_CHECK " Реестр")) {
        if (ImGui::MenuItem(ICON_FA_DATABASE " Прогоны и фильтры")) {
            CreateView<RunsView>("Реестр прогонов");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_CHART_PIE " Отчёты")) {
        if (ImGui::MenuItem(ICON_FA_FILE_LINES " Сводка и экспорт")) {
            CreateView<ReportView>("Отчёт");
        }
        if (ImGui::MenuItem(ICON_FA_CODE_COMPARE " Сравнение прогонов")) {
            CreateView<CompareView>("Сравнение прогонов");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_PAINTBRUSH " Вид")) {
        if (ImGui::BeginMenu("Тема")) {
            if (ImGui::MenuItem("Тёмная")) ApplyTheme(0);
            if (ImGui::MenuItem("Светлая")) ApplyTheme(1);
            if (ImGui::MenuItem("Классическая")) ApplyTheme(2);
            if (ImGui::MenuItem("Сине-серая")) ApplyTheme(3);
            ImGui::EndMenu();
        }
        ImGui::SliderInt("Шрифт", &fontSize, 14, 40);
        ApplyFont(fontSize);

        if (ImGui::BeginMenu(ICON_FA_TABLE "  Открытые формы")) {
            if (views.empty()) ImGui::TextDisabled("Нет открытых форм");
            for (auto& view : views) {
                if (ImGui::MenuItem(view->GetTitle(), ICON_FA_XMARK "  закрыть")) {
                    CloseView(view.get());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH "  Закрыть все формы")) CloseAllViews();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_CIRCLE_INFO " Справка")) {
        if (ImGui::MenuItem(ICON_FA_GEAR " Настройки")) CreateView<SettingsView>();
        if (ImGui::MenuItem(ICON_FA_BOOK " О программе")) showAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void App::RenderStatusBar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - 36), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 36), ImGuiCond_Always);
    if (ImGui::Begin("Статус", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::Text("%s", statusLine.empty() ? "Готово" : statusLine.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("·  форм открыто: %zu", views.size());
        ImGui::SameLine();
        ImGui::TextDisabled("·  Ctrl+W — закрыть активную форму");
        ImGui::SameLine();
        ImGui::TextDisabled("·  база: %s", dbPath.empty() ? "не выбрана" : dbPath.c_str());
        ImGui::End();
    }
}

void App::RenderErrorPopup() {
    if (errorMessage.empty()) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Ошибка", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", errorMessage.c_str());
        if (ImGui::Button("Понятно")) errorMessage.clear();
        ImGui::End();
    }
}

void App::RenderViews() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyMods & ImGuiMod_Ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) CloseFocusedView();
    }

    BaseView* currentFocus = nullptr;
    for (auto& view : views) {
        if (!view->IsVisible) continue;
        if (view->WantsDock && mainDockId) {
            ImGui::SetNextWindowDockID(mainDockId, ImGuiCond_Always);
            if (view->DockFrames > 0) --view->DockFrames;
            else view->WantsDock = false;
        }
        view->Render();
        if (!view->IsOpen) view->IsVisible = false;
        if (view->IsVisible && view->IsFocused) currentFocus = view.get();
    }

    auto keepEnd = std::remove_if(views.begin(), views.end(),
                                  [](const std::unique_ptr<BaseView>& view) {
                                      return !view->IsVisible;
                                  });
    if (keepEnd != views.end()) views.erase(keepEnd, views.end());
    if (!currentFocus && !views.empty()) currentFocus = views.back().get();
    focusedView = currentFocus;

    RenderStatusBar();
    RenderErrorPopup();
}
