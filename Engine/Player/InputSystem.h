#pragma once
// ============================================================
//  Engine/InputSystem.h  —  ДВИЖОК, не трогаешь
//  Синглтон системы ввода (клавиатура + мышь).
//  Игра использует: isKeyDown(), isKeyPressed(), getMouseDelta()
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_set>
#include <iostream>

// ── Перечисление клавиш (обёртка над GLFW) ───────────────
enum class Key : int {
    W         = GLFW_KEY_W,
    A         = GLFW_KEY_A,
    S         = GLFW_KEY_S,
    D         = GLFW_KEY_D,
    Space     = GLFW_KEY_SPACE,
    LShift    = GLFW_KEY_LEFT_SHIFT,
    RShift    = GLFW_KEY_RIGHT_SHIFT,
    LCtrl     = GLFW_KEY_LEFT_CONTROL,
    RCtrl     = GLFW_KEY_RIGHT_CONTROL,
    R         = GLFW_KEY_R,
    F         = GLFW_KEY_F,
    E         = GLFW_KEY_E,
    Q         = GLFW_KEY_Q,
    Tab       = GLFW_KEY_TAB,
    Escape    = GLFW_KEY_ESCAPE,
    Alpha1    = GLFW_KEY_1,
    Alpha2    = GLFW_KEY_2,
    Alpha3    = GLFW_KEY_3,
    Alpha4    = GLFW_KEY_4,
    Alpha5    = GLFW_KEY_5,
};

enum class Mouse : int {
    Left   = GLFW_MOUSE_BUTTON_LEFT,
    Right  = GLFW_MOUSE_BUTTON_RIGHT,
    Middle = GLFW_MOUSE_BUTTON_MIDDLE,
};

class InputSystem {
public:
    static InputSystem& get() {
        static InputSystem instance;
        return instance;
    }

    // ── Инициализация (вызывает Engine::init) ─────────────
    void init(GLFWwindow* win) {
        window = win;

        // Запомнить начальную позицию мыши
        glfwGetCursorPos(win, &lastMouseX, &lastMouseY);

        // Колесо мыши — через callback
        glfwSetScrollCallback(win, _scrollCallback);

        std::cout << "[Input] InputSystem ready\n";
    }

    // ── Опрос состояния (вызывает Engine::run каждый кадр) ──
    void poll() {
        // Сохраняем предыдущее состояние
        prevKeys  = currKeys;
        prevMouse = currMouse;

        // Обновляем текущее состояние клавиш
        currKeys.clear();
        // GLFW не даёт список нажатых — проверяем только те что нам важны
        static const int watchKeys[] = {
            GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D,
            GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT,
            GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
            GLFW_KEY_R, GLFW_KEY_F, GLFW_KEY_E, GLFW_KEY_Q,
            GLFW_KEY_TAB, GLFW_KEY_ESCAPE,
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
        };
        for (int k : watchKeys)
            if (glfwGetKey(window, k) == GLFW_PRESS)
                currKeys.insert(k);

        // Кнопки мыши
        currMouse.clear();
        for (int b : {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE})
            if (glfwGetMouseButton(window, b) == GLFW_PRESS)
                currMouse.insert(b);

        // Дельта мыши
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        mouseDelta.x = (float)(mx - lastMouseX);
        mouseDelta.y = (float)(my - lastMouseY);
        lastMouseX   = mx;
        lastMouseY   = my;

        glfwPollEvents();
    }

    // ── Клавиатура ────────────────────────────────────────
    // Зажата прямо сейчас
    bool isKeyDown(Key k) const {
        return currKeys.count((int)k) > 0;
    }
    // Нажата именно в этом кадре (не удерживается)
    bool isKeyPressed(Key k) const {
        return currKeys.count((int)k) > 0 && prevKeys.count((int)k) == 0;
    }
    // Отпущена именно в этом кадре
    bool isKeyReleased(Key k) const {
        return currKeys.count((int)k) == 0 && prevKeys.count((int)k) > 0;
    }

    // ── Мышь ──────────────────────────────────────────────
    bool isMouseDown(Mouse b) const {
        return currMouse.count((int)b) > 0;
    }
    bool isMousePressed(Mouse b) const {
        return currMouse.count((int)b) > 0 && prevMouse.count((int)b) == 0;
    }
    bool isMouseReleased(Mouse b) const {
        return currMouse.count((int)b) == 0 && prevMouse.count((int)b) > 0;
    }

    // Дельта мыши за последний кадр (пиксели)
    glm::vec2 getMouseDelta() const { return mouseDelta; }

    // Дельта колеса мыши (сбрасывается каждый кадр)
    float getScrollDelta() const { return scrollDelta; }

    // ── Курсор ────────────────────────────────────────────
    void setCursorLocked(bool locked) {
        if (!window) return;
        glfwSetInputMode(window, GLFW_CURSOR,
            locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }

    bool isCursorLocked() const {
        if (!window) return false;
        return glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }

private:
    InputSystem()  = default;
    ~InputSystem() = default;
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    GLFWwindow* window = nullptr;

    std::unordered_set<int> currKeys;
    std::unordered_set<int> prevKeys;
    std::unordered_set<int> currMouse;
    std::unordered_set<int> prevMouse;

    glm::vec2 mouseDelta  = glm::vec2(0.f);
    double    lastMouseX  = 0.0;
    double    lastMouseY  = 0.0;

    // scroll обновляется через callback
    float scrollDelta = 0.f;

    static void _scrollCallback(GLFWwindow*, double, double yoffset) {
        InputSystem::get().scrollDelta = (float)yoffset;
    }
};
