#include "imgui.h"
#include "imgui_internal.h"

#include "App.h"
#include "views/FormView.h"
#include "views/ImportView.h"
#include "views/SettingsView.h"

#include <cstdio>
#include <string>

static void Frame(App& app) {
    ImGui::NewFrame();
    app.RenderMenuBar();
    app.RenderViews();
    ImGui::Render();
}

static bool HasView(App& app, const std::string& title) {
    for (auto& view : app.views) {
        std::string t = view->GetTitle();
        size_t cut = t.find("###");
        if (cut != std::string::npos) t = t.substr(0, cut);
        if (t == title) return true;
    }
    return false;
}

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);

    App app;
    app.ApplyTheme(0);
    unsigned char* pixels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texW, &texH);

    BaseView* form = app.CreateView<FormView>("Форма МЗ");
    BaseView* settings = app.CreateView<SettingsView>();
    printf("ok: created %zu views\n", app.views.size());
    if (app.views.size() != 2) return 1;

    for (int i = 0; i < 3; ++i) Frame(app);
    if (app.views.size() != 2) { printf("fail: views vanished while open\n"); return 1; }
    if (!form->IsOpen || !settings->IsOpen) { printf("fail: views not open\n"); return 1; }
    printf("ok: views stay open across frames\n");

    // 1) кнопка «Закрыть форму» внутри самой формы
    form->RequestClose();
    Frame(app);
    if (app.views.size() != 1 || HasView(app, "Форма МЗ")) {
        printf("fail: RequestClose did not close the form (%zu)\n", app.views.size());
        return 1;
    }
    printf("ok: RequestClose closed the form\n");

    // 2) закрытие через меню (App::CloseView)
    app.CloseView(settings);
    Frame(app);
    if (!app.views.empty()) { printf("fail: CloseView did not remove the view\n"); return 1; }
    printf("ok: CloseView removed the view\n");

    // 3) Ctrl+W закрывает активную форму
    app.CreateView<ImportView>("Импорт оборота");
    app.CreateView<FormView>("Форма МЗ");
    Frame(app);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_W, true);
    Frame(app);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_W, false);
    if (app.views.size() != 1) {
        printf("fail: Ctrl+W closed %zu views instead of 1\n", app.views.size());
        return 1;
    }
    printf("ok: Ctrl+W closed the focused form\n");

    // 4) «Закрыть все формы»
    app.CloseAllViews();
    Frame(app);
    if (!app.views.empty()) { printf("fail: CloseAllViews left views behind\n"); return 1; }
    printf("ok: CloseAllViews cleared everything\n");

    // 5) повторное открытие той же формы работает
    app.CreateView<FormView>("Форма МЗ");
    Frame(app);
    if (app.views.size() != 1) { printf("fail: reopened view missing\n"); return 1; }
    printf("ok: reopened form after closing\n");

    ImGui::DestroyContext();
    printf("test_views: OK\n");
    return 0;
}
