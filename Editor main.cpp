#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Settings.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "TextRenderer.h"
#include "MapEditor.h"

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

        mapEditor.update(gWindow, dt);
        mapEditor.draw();

        glfwSwapBuffers(gWindow);
        glfwPollEvents();
    }

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