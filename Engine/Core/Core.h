#pragma once
// ============================================================
//  Engine/Core.h  —  ДВИЖОК, не трогаешь
//  Главный класс движка. Владеет окном, сценой, системами.
//  Игра наследует GameBase и переопределяет onStart/onUpdate.
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>

#include "Scene.h"
#include "Render/Renderer.h"
#include "InputSystem.h"
#include "Physics.h"
#include "AudioManager.h"

// ── Конфиг движка ──────────────────────────────────────────
struct EngineConfig {
    std::string title      = "P.L.A.Y.B.A.C.K.";
    int         width      = 1280;
    int         height     = 720;
    bool        vsync      = true;
    bool        fullscreen = false;
    int         msaa       = 4;
};

// ── Базовый класс игры ─────────────────────────────────────
// GameScene наследует от этого
class GameBase {
public:
    Scene* scene = nullptr;

    virtual ~GameBase() = default;

    // Создать все объекты сцены — вызывается один раз
    virtual void onStart()          = 0;

    // Игровая логика поверх компонентов — каждый кадр
    virtual void onUpdate(float dt) {}

    // ImGui / HUD — после основного рендера
    virtual void onRenderUI()       {}

    // Очистка — вызывается при закрытии
    virtual void onShutdown()       {}
};

// ── Движок ─────────────────────────────────────────────────
class Engine {
public:
    Scene scene;

    // ── Инициализация ─────────────────────────────────────
    bool init(const EngineConfig& cfg = {}) {
        config = cfg;

        // GLFW
        if (!glfwInit()) {
            std::cerr << "[Engine] glfwInit failed\n";
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        if (cfg.msaa > 0)
            glfwWindowHint(GLFW_SAMPLES, cfg.msaa);

        GLFWmonitor* monitor = cfg.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        window = glfwCreateWindow(cfg.width, cfg.height,
                                  cfg.title.c_str(), monitor, nullptr);
        if (!window) {
            std::cerr << "[Engine] Window creation failed\n";
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        // Resize
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
            glViewport(0, 0, w, h);
        });

        // Подсистемы
        if (!Renderer::get().init(window)) {
            std::cerr << "[Engine] Renderer init failed\n";
            return false;
        }

        InputSystem::get().init(window);

        if (!PhysicsWorld::get().init()) {
            std::cerr << "[Engine] Physics init failed\n";
            return false;
        }

        if (!AudioManager::get().init()) {
            std::cerr << "[Engine] Audio init failed\n";
            return false;
        }

        if (cfg.msaa > 0)
            glEnable(GL_MULTISAMPLE);

        std::cout << "[Engine] Initialized — " << cfg.title
                  << " (" << cfg.width << "x" << cfg.height << ")\n";
        return true;
    }

    // ── Главный цикл ──────────────────────────────────────
    void run(GameBase* game) {
        if (!game) return;

        game->scene = &scene;
        game->onStart();

        double lastTime = glfwGetTime();

        while (!glfwWindowShouldClose(window)) {
            // Delta time
            double now = glfwGetTime();
            float  dt  = (float)(now - lastTime);
            lastTime   = now;
            // Cap: минимум 20 FPS чтобы не скакало при лаге
            if (dt > 0.05f) dt = 0.05f;

            // ── Input ──────────────────────────────────────
            InputSystem::get().poll();

            // ── Physics ────────────────────────────────────
            PhysicsWorld::get().step(dt);

            // ── Update компонентов сцены ───────────────────
            scene.update(dt);

            // ── Игровая логика ─────────────────────────────
            game->onUpdate(dt);

            // ── Render ─────────────────────────────────────
            Renderer::get().beginFrame();
            Renderer::get().drawScene(scene);
            game->onRenderUI();
            Renderer::get().endFrame();
        }

        game->onShutdown();
        _shutdown();
    }

    GLFWwindow*        getWindow() const { return window; }
    const EngineConfig& getConfig() const { return config; }

private:
    GLFWwindow*  window = nullptr;
    EngineConfig config;

    void _shutdown() {
        scene.clear();
        AudioManager::get().shutdown();
        PhysicsWorld::get().shutdown();
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << "[Engine] Shutdown complete\n";
    }
};
