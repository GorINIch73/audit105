#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "AboutDialog.h"
#include "App.h"
#include "IconsFontAwesome6.h"
#include "ImGuiFileDialog.h"
#include "PlatformUtils.h"

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

static std::string ParseArg(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag && i + 1 < argc) return argv[i + 1];
    }
    return {};
}

static void HandleDbDialog(App& app, const char* key, bool saveAs) {
    if (!ImGuiFileDialog::Instance()->Display(key)) return;
    if (ImGuiFileDialog::Instance()->IsOk()) {
        const std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
        if (saveAs) app.SaveDatabaseAs(path);
        else app.OpenDatabase(path);
    }
    ImGuiFileDialog::Instance()->Close();
}

int main(int argc, char** argv) {
    constexpr int kMaxFps = 60;
    constexpr int kIdleFps = 15;
    constexpr auto kMinFrameTime = std::chrono::duration<double>(1.0 / kMaxFps);
    constexpr double kIdleWaitTime = 1.0 / kIdleFps;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Аудит счёта 105", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static std::string iniPath = AppDataDir() + "/imgui.ini";
    io.IniFilename = iniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    App app;
    app.SetWindow(window);
    app.SetDbPath(ParseArg(argc, argv, "--db").empty() ? DefaultDbPath() : ParseArg(argc, argv, "--db"));
    app.refsPath = ParseArg(argc, argv, "--refs").empty() ? "refs" : ParseArg(argc, argv, "--refs");

    app.ApplyTheme(0);
    app.ApplyFont(22);
    app.OpenDatabase(app.DbPath());

    AboutInfo about;
    about.dbPath = app.DbPath();
    about.refsPath = app.refsPath;

    while (!glfwWindowShouldClose(window)) {
        const auto frameStart = std::chrono::steady_clock::now();

        const bool isIdle = app.views.empty() && !app.showAbout &&
                            !app.isImporting && !app.HasError();
        if (isIdle) {
            glfwWaitEventsTimeout(kIdleWaitTime);
        } else {
            glfwPollEvents();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID mainDockId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        ImGuiDockNode* centralDockNode = ImGui::DockBuilderGetCentralNode(mainDockId);
        app.SetMainDockId(centralDockNode ? centralDockNode->ID : mainDockId);

        app.RenderMenuBar();

        HandleDbDialog(app, "NewDbDlg", false);
        HandleDbDialog(app, "OpenDbDlg", false);
        HandleDbDialog(app, "SaveDbAsDlg", true);

        app.RenderViews();

        if (app.showAbout) {
            about.dbPath = app.DbPath();
            about.refsPath = app.refsPath;
            RenderAboutDialog(about, &app.showAbout);
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        const auto frameTime = std::chrono::steady_clock::now() - frameStart;
        if (frameTime < kMinFrameTime) {
            std::this_thread::sleep_for(kMinFrameTime - frameTime);
        }
    }

    app.SaveAllViews();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
