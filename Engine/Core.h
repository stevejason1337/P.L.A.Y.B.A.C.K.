#pragma once

// ============================================================
//  Engine/Core.h  —  ДВИЖОК, не трогаешь
//  Главный класс движка. Владеет окном, сценой, системами.
//  Игра наследует GameBase и переопределяет onStart/onUpdate.
// ============================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <functional>
#include <string>

#include "Scene.h"
#include "Renderer.h"
#include "InputSystem.h"
#include "Physics.h"
#include "AudioManager.h"

// ── Конфиг движка ──────────────────────────────────────────
struct EngineConfig {
    std::string title  = "P.L.A.Y.B.A.C.K.";
    int         width  = 1280;
    int         height = 720;
    bool        vsync  = true;
    bool        fullscreen = false;
    int         msaa   = 4;      // 0 = off
};

// ── Базовый класс игры ─────────────────────────────────────
// Твой GameScene.h наследует от этого
class GameBase {
public:
    Scene* scene   = nullptr;
    virtual ~GameBase() = default;

    virtual void onStart()               = 0;  // создать объекты сцены
    virtual void onUpdate(float dt)      {}    // игровая логика поверх компонентов
    virtual void onRenderUI()            {}    // ImGui / HUD
    virtual void onShutdown()            {}
};

// ── Движок ─────────────────────────────────────────────────
class Engine {
public:
    Scene  scene;

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
        if (cfg.msaa > 0) glfwWindowHint(GLFW_SAMPLES, cfg.msaa);

        GLFWmonitor* monitor = cfg.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        window = glfwCreateWindow(cfg.width, cfg.height, cfg.title.c_str(), monitor, nullptr);
        if (!window) { std::cerr << "[Engine] Window creation failed\n"; return false; }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        // Resize callback
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
            glViewport(0, 0, w, h);
        });

        // Подсистемы
        if (!Renderer::get().init(window))       return false;
        InputSystem::get().init(window);
        if (!PhysicsWorld::get().init())         return false;
        if (!AudioManager::get().init())         return false;

        if (cfg.msaa > 0) glEnable(GL_MULTISAMPLE);

        std::cout << "[Engine] Initialized — " << cfg.title << "\n";
        return true;
    }

    // Запустить игру
    void run(GameBase* game) {
        game->scene = &scene;
        game->onStart();

        double lastTime = glfwGetTime();

        while (!glfwWindowShouldClose(window)) {
            double now = glfwGetTime();
            float  dt  = (float)(now - lastTime);
            lastTime   = now;
            dt = glm::clamp(dt, 0.f, 0.05f);   // cap at 20fps min

            // Input
            InputSystem::get().poll();

            // ESC — выход (можно переопределить в игре)
            if (InputSystem::get().isKeyPressed(GLFW_KEY_ESCAPE))
                glfwSetWindowShouldClose(window, true);

            // Physics
            PhysicsWorld::get().step(dt);

            // Обновить все компоненты
            scene.update(dt);

            // Игровая логика поверх
            game->onUpdate(dt);

            // Render
            Renderer::get().beginFrame();
            Renderer::get().drawScene(scene);
            game->onRenderUI();
            Renderer::get().endFrame();
        }

        game->onShutdown();
        shutdown();
    }

    GLFWwindow* getWindow() const { return window; }
    const EngineConfig& getConfig() const { return config; }

private:
    GLFWwindow*  window = nullptr;
    EngineConfig config;

    void shutdown() {
        scene.clear();
        PhysicsWorld::get().shutdown();
        AudioManager::get().shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << "[Engine] Shutdown complete\n";
    }
};
