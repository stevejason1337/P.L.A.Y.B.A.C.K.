#pragma once

// ============================================================
//  Engine/InputSystem.h  —  ДВИЖОК, не трогаешь
//  Обёртка над GLFW. Игра спрашивает "нажата ли клавиша",
//  ничего не знает про GLFW напрямую.
// ============================================================

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>

class InputSystem {
public:
    // ── Singleton ──────────────────────────────────────────
    static InputSystem& get() {
        static InputSystem instance;
        return instance;
    }

    void init(GLFWwindow* window) {
        win = window;
        glfwSetKeyCallback(window, keyCallback);
        glfwSetCursorPosCallback(window, mousePosCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetScrollCallback(window, scrollCallback);
    }

    // Вызывать в начале каждого кадра
    void poll() {
        prevKeys        = currKeys;
        prevMouseBtns   = currMouseBtns;
        mouseDelta      = glm::vec2(0.f);
        scrollDelta     = 0.f;
        glfwPollEvents();
    }

    // ── Клавиатура ─────────────────────────────────────────
    bool isKeyDown    (int glfwKey) const { return currKeys.count(glfwKey) && currKeys.at(glfwKey); }
    bool isKeyPressed (int glfwKey) const { return isKeyDown(glfwKey) && !(prevKeys.count(glfwKey) && prevKeys.at(glfwKey)); }
    bool isKeyReleased(int glfwKey) const { return !isKeyDown(glfwKey) && (prevKeys.count(glfwKey) && prevKeys.at(glfwKey)); }

    // ── Мышь ───────────────────────────────────────────────
    bool isMouseDown    (int btn) const { return currMouseBtns.count(btn) && currMouseBtns.at(btn); }
    bool isMousePressed (int btn) const { return isMouseDown(btn) && !(prevMouseBtns.count(btn) && prevMouseBtns.at(btn)); }
    bool isMouseReleased(int btn) const { return !isMouseDown(btn) && (prevMouseBtns.count(btn) && prevMouseBtns.at(btn)); }

    glm::vec2 getMousePos()   const { return mousePos; }
    glm::vec2 getMouseDelta() const { return mouseDelta; }
    float     getScrollDelta()const { return scrollDelta; }

    void setCursorLocked(bool locked) {
        glfwSetInputMode(win, GLFW_CURSOR,
            locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        cursorLocked = locked;
    }
    bool isCursorLocked() const { return cursorLocked; }

private:
    GLFWwindow* win = nullptr;
    bool cursorLocked = false;

    std::unordered_map<int,bool> currKeys, prevKeys;
    std::unordered_map<int,bool> currMouseBtns, prevMouseBtns;

    glm::vec2 mousePos   = {0,0};
    glm::vec2 mouseDelta = {0,0};
    float     scrollDelta = 0.f;
    glm::vec2 lastMousePos = {0,0};
    bool      firstMouse = true;

    // ── GLFW callbacks (static) ────────────────────────────
    static void keyCallback(GLFWwindow*, int key, int, int action, int) {
        auto& in = get();
        if (action == GLFW_PRESS)   in.currKeys[key] = true;
        if (action == GLFW_RELEASE) in.currKeys[key] = false;
    }
    static void mousePosCallback(GLFWwindow*, double x, double y) {
        auto& in = get();
        glm::vec2 pos = {(float)x, (float)y};
        if (in.firstMouse) { in.lastMousePos = pos; in.firstMouse = false; }
        in.mouseDelta  = pos - in.lastMousePos;
        in.lastMousePos = pos;
        in.mousePos     = pos;
    }
    static void mouseButtonCallback(GLFWwindow*, int btn, int action, int) {
        auto& in = get();
        in.currMouseBtns[btn] = (action == GLFW_PRESS);
    }
    static void scrollCallback(GLFWwindow*, double, double yOff) {
        get().scrollDelta = (float)yOff;
    }
};

// Удобные алиасы — используй в игровом коде
namespace Key {
    constexpr int W      = GLFW_KEY_W;
    constexpr int A      = GLFW_KEY_A;
    constexpr int S      = GLFW_KEY_S;
    constexpr int D      = GLFW_KEY_D;
    constexpr int Space  = GLFW_KEY_SPACE;
    constexpr int LShift = GLFW_KEY_LEFT_SHIFT;
    constexpr int LCtrl  = GLFW_KEY_LEFT_CONTROL;
    constexpr int Escape = GLFW_KEY_ESCAPE;
    constexpr int R      = GLFW_KEY_R;
    constexpr int F      = GLFW_KEY_F;
    constexpr int Tab    = GLFW_KEY_TAB;
    constexpr int E      = GLFW_KEY_E;
    constexpr int G      = GLFW_KEY_G;
    constexpr int F1     = GLFW_KEY_F1;
    constexpr int F2     = GLFW_KEY_F2;
    constexpr int F3     = GLFW_KEY_F3;
}
namespace Mouse {
    constexpr int Left   = GLFW_MOUSE_BUTTON_LEFT;
    constexpr int Right  = GLFW_MOUSE_BUTTON_RIGHT;
    constexpr int Middle = GLFW_MOUSE_BUTTON_MIDDLE;
}
