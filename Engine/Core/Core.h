#pragma once
// ============================================================
//  Engine/Core/Core.h
// ============================================================

#include <windows.h>
#include <iostream>
#include <string>
#include <chrono>

#include "../Render/Renderer.h"

// ─────────────────────────────────────────────────────────────
//  IScene  —  интерфейс для GameScene
// ─────────────────────────────────────────────────────────────
class IScene {
public:
    virtual ~IScene() = default;
    virtual bool onInit(Renderer& renderer) = 0;
    virtual void onUpdate(float dt) = 0;
    virtual void onRender(Renderer& renderer, float dt) = 0;
    virtual void onShutdown() = 0;
};

// ─────────────────────────────────────────────────────────────
//  EngineConfig
// ─────────────────────────────────────────────────────────────
struct EngineConfig {
    std::string title = "Game";
    int         width = 1280;
    int         height = 720;
    bool        vsync = true;
    bool        fullscreen = false;
    int         msaa = 1;
};

// ─────────────────────────────────────────────────────────────
//  Engine
// ─────────────────────────────────────────────────────────────
class Engine {
public:
    Engine() = default;
    ~Engine() { shutdown(); }

    bool init(const EngineConfig& cfg) {
        m_cfg = cfg;

        HINSTANCE hInst = GetModuleHandle(nullptr);

        WNDCLASSEX wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = _WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PLAYBACKWindowClass";
        if (!RegisterClassEx(&wc)) return false;

        RECT rc{ 0, 0, cfg.width, cfg.height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        std::wstring title(cfg.title.begin(), cfg.title.end());
        m_hWnd = CreateWindowEx(0, L"PLAYBACKWindowClass",
            title.c_str(), WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInst, this);

        if (!m_hWnd) return false;

        ShowWindow(m_hWnd, SW_SHOWDEFAULT);
        UpdateWindow(m_hWnd);

        if (!m_renderer.Initialize(m_hWnd, cfg.width, cfg.height)) {
            std::cerr << "[Engine] Renderer init failed!\n";
            return false;
        }

        std::cout << "[Engine] Init OK — " << cfg.width << "x" << cfg.height << "\n";
        return true;
    }

    void run(IScene* scene) {
        if (!scene) return;
        if (!scene->onInit(m_renderer)) {
            std::cerr << "[Engine] Scene init failed!\n";
            return;
        }

        using Clock = std::chrono::high_resolution_clock;
        auto prev = Clock::now();
        MSG msg{};
        m_running = true;

        while (m_running) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) m_running = false;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (!m_running) break;

            auto  now = Clock::now();
            float dt = std::chrono::duration<float>(now - prev).count();
            prev = now;
            if (dt > 0.1f) dt = 0.1f;

            scene->onUpdate(dt);
            m_renderer.BeginFrame();
            scene->onRender(m_renderer, dt);
            m_renderer.EndFrame();
        }

        scene->onShutdown();
    }

    Renderer& getRenderer() { return m_renderer; }
    HWND      getHWnd() { return m_hWnd; }

private:
    EngineConfig m_cfg;
    Renderer     m_renderer;
    HWND         m_hWnd = nullptr;
    bool         m_running = false;

    void shutdown() {
        if (m_hWnd) { DestroyWindow(m_hWnd); m_hWnd = nullptr; }
    }

    static LRESULT CALLBACK _WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        Engine* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = reinterpret_cast<Engine*>(cs->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else {
            self = reinterpret_cast<Engine*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }
        switch (msg) {
        case WM_DESTROY: PostQuitMessage(0);    return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
};