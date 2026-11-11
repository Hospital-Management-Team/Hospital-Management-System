// Hospital Management System - graphical front end (Dear ImGui + GLFW + OpenGL).
//
// Usage:  hms_gui [--store PATH]
//   --store PATH   data store file (default: database/hospital.db)
//
// The window code is here; everything on screen is drawn by gui::App (gui_app.cpp).

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "data/schema.hpp"
#include "gui/gui_app.hpp"
#include "gui/ui.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace {

void printUsage() {
    std::cout << "Usage: hms_gui [--store PATH]\n"
                 "  --store PATH   data store file (default: database/hospital.db)\n"
                 "  --help         show this help\n"
                 "  --version      show the version\n";
}

void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

// A readable system font; Dear ImGui's built-in font is used when none is found.
void loadFont(float sizePixels) {
    static const char* const kCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : kCandidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path, sizePixels) != nullptr) return;
    }
    ImFontConfig config;
    config.SizePixels = sizePixels;
    io.Fonts->AddFontDefault(&config);
}

}  // namespace

int main(int argc, char** argv) {
    hms::gui::StartupOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--version") {
            std::cout << "HMS 0.1.0 (graphical), schema version " << hms::schema::currentVersion() << "\n";
            return 0;
        }
        if (arg == "--store" && i + 1 < argc) {
            options.storePath = argv[++i];
            continue;
        }
        std::cerr << "Unknown option: " << arg << "\n";
        printUsage();
        return 2;
    }

    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() == GLFW_FALSE) return 1;

    // OpenGL 3.0 with GLSL 130 is the oldest the Dear ImGui backend supports.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    float scale = 1.0F;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        float sx = 1.0F;
        float sy = 1.0F;
        glfwGetMonitorContentScale(monitor, &sx, &sy);
        if (sx > 0.0F) scale = sx;
    }

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(1280.0F * scale), static_cast<int>(800.0F * scale),
                                          "Hospital Management System", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no window layout file is written next to the data
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Tab and Enter work without the mouse
    loadFont(17.0F * scale);
    hms::gui::applyTheme(scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        hms::gui::App app(options);
        while (glfwWindowShouldClose(window) == GLFW_FALSE && !app.wantsQuit()) {
            glfwPollEvents();
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
                glfwWaitEventsTimeout(0.1);
                continue;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            app.frame();

            ImGui::Render();
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.955F, 0.965F, 0.975F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }  // the store is closed and its lock released here

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
