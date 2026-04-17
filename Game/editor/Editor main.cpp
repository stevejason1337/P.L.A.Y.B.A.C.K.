#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Settings.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "TextRenderer.h"
#include "MapEditor.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// ──────────────────────────────────────────────
//  Editor.exe
//
//  Controls:
//    RMB + WASD    — fly camera
//    Shift         — fast fly
//    Enter / F     — place selected palette object
//    LMB           — select object / click palette
//    G / R / T     — translate / rotate / scale mode
//    Arrows / Q/E  — move|rotate|scale selected
//    Del           — delete
//    Ctrl+D        — duplicate
//    Z             — snap to ground
//    Tab           — toggle grid
//    Home          — toggle snap
//    PgUp/Dn       — change palette
//    Ctrl+S        — save  → maps/level.map
//    Ctrl+O        — load  ← maps/level.map
//    F5            — launch Game.exe and close editor
//    Escape        — deselect object
// ──────────────────────────────────────────────

// Forward: input callbacks
void ed_key_cb(GLFWwindow*, int key, int, int action, int mods);
void ed_mb_cb(GLFWwindow*, int btn, int action, int);
void ed_mm_cb(GLFWwindow*, double x, double y);
void ed_scroll_cb(GLFWwindow*, double x, double y);
void ed_resize_cb(GLFWwindow*, int w, int h);

static GLFWwindow* gWindow = nullptr;

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    gWindow = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "Map Editor  |  Ctrl+S=Save  F5=Play  RMB+WASD=Fly", NULL, NULL);
    if (!gWindow) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(gWindow);
    glfwSetKeyCallback(gWindow, ed_key_cb);
    glfwSetMouseButtonCallback(gWindow, ed_mb_cb);
    glfwSetCursorPosCallback(gWindow, ed_mm_cb);
    glfwSetScrollCallback(gWindow, ed_scroll_cb);
    glfwSetFramebufferSizeCallback(gWindow, ed_resize_cb);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& imio = ImGui::GetIO();
    imio.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.f;
    style.FrameRounding = 4.f;
    style.ScrollbarRounding = 4.f;
    style.GrabRounding = 4.f;
    style.WindowBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.94f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.08f, 1.f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.18f, 0.30f, 1.f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.14f, 0.28f, 0.50f, 1.f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.38f, 0.64f, 1.f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.14f, 0.28f, 0.50f, 1.f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.38f, 0.64f, 1.f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.46f, 0.76f, 1.f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.46f, 0.76f, 1.f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.22f, 0.27f, 1.f);
    ImGui_ImplGlfw_InitForOpenGL(gWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Font must be initialised before anything that draws text
    textRenderer.init("cour.ttf", 16.f, SCR_WIDTH, SCR_HEIGHT);

    // Load map terrain (read-only background)
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    std::cout << "[EDITOR] Loading map terrain...\n";
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    std::cout << "[EDITOR] Building BVH for placement raycasts...\n";
    bvh.build(colTris);

    // Init editor
    mapEditor.init(mapMeshes, mapT);

    // Auto-load existing map if it exists
    mapEditor.load();

    float dt = 0.f, last = 0.f;

    while (!glfwWindowShouldClose(gWindow))
    {
        float now = (float)glfwGetTime();
        dt = now - last; last = now;
        if (dt > 0.1f) dt = 0.1f;

        // ImGui new frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        mapEditor.update(gWindow, dt);
        mapEditor.draw();

        // ImGui render
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(gWindow);
        glfwPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}

// ──────────────────────────────────────────────
//  Callbacks
// ──────────────────────────────────────────────
void ed_key_cb(GLFWwindow* w, int key, int, int action, int)
{
    // F5 = save and launch game
    if (key == GLFW_KEY_F5 && action == GLFW_PRESS) {
        mapEditor.save();
        std::cout << "[EDITOR] Launching Game.exe...\n";
#ifdef _WIN32
        system("start Game.exe");
#else
        system("./Game &");
#endif
        return;
    }

    // Quit editor
    if (key == GLFW_KEY_F4 && action == GLFW_PRESS &&
        (glfwGetKey(w, GLFW_KEY_LEFT_ALT) == GLFW_PRESS))
        glfwSetWindowShouldClose(w, GLFW_TRUE);

    mapEditor.onKey(w, key, action);
}

void ed_mb_cb(GLFWwindow* w, int btn, int action, int)
{
    mapEditor.onMouseButton(w, btn, action);
}

void ed_mm_cb(GLFWwindow* w, double x, double y)
{
    mapEditor.onMouseMove(w, x, y);
}

void ed_scroll_cb(GLFWwindow*, double x, double y)
{
    mapEditor.onScroll(x, y);
}

void ed_resize_cb(GLFWwindow*, int w, int h)
{
    glViewport(0, 0, w, h);
}