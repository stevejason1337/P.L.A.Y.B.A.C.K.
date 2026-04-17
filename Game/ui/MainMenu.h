#pragma once
// ═══════════════════════════════════════════════════════════════
//  MainMenu.h  — красивое главное меню + экран загрузки
//
//  Использование в main.cpp:
//    #include "MainMenu.h"
//    // В начале main loop вместо прямого запуска:
//    if (gMenu.state != MenuState::INGAME) {
//        gMenu.draw(window, dt);
//        // рендер / swap
//        continue;
//    }
// ═══════════════════════════════════════════════════════════════

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#ifdef _WIN32
#include "imgui_impl_dx11.h"
#endif
#include "Settings.h"
#include "TAA.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <functional>
#include <cmath>

// ─── Состояния меню ───────────────────────────────────────────
enum class MenuState {
    MAIN,       // главное меню
    SETTINGS,   // настройки
    LOADING,    // экран загрузки
    INGAME      // игра запущена
};

// ─── Режим окна ───────────────────────────────────────────────
enum class WindowMode { WINDOWED, BORDERLESS, FULLSCREEN };

// ─── Пресеты разрешений ───────────────────────────────────────
struct ResPreset { int w, h; const char* label; };
static const ResPreset kResPresets[] = {
    {1280,  720,  "1280×720"},
    {1600,  900,  "1600×900"},
    {1920, 1080,  "1920×1080"},
    {2560, 1440,  "2560×1440"},
};
static const int kResCount = 4;

// ─── Сообщения загрузки ───────────────────────────────────────
static const char* kLoadMsgs[] = {
    "Initializing renderer...",
    "Loading map...",
    "Loading enemy models...",
    "Loading weapons...",
    "Initializing physics...",
    "Loading sounds...",
    "Final preparation...",
};
static const int kLoadMsgCount = 7;

// ═══════════════════════════════════════════════════════════════
struct MainMenu {

    MenuState   state = MenuState::MAIN;
    WindowMode  winMode = WindowMode::WINDOWED;
    int         resIndex = 0;   // индекс в kResPresets
    int         apiIndex = 0;   // 0=DX11 1=OpenGL
    bool        settingsChanged = false;
    bool        taaEnabled = true;

    // Анимация
    float       time = 0.f;
    float       fadeAlpha = 1.f;   // 1=чёрный экран, 0=видно
    bool        fadingIn = true;  // true=появляемся
    bool        fadingOut = false; // true=уходим в загрузку

    // Загрузка
    float       loadProgress = 0.f;
    int         loadMsgIdx = 0;
    float       loadMsgTimer = 0.f;
    bool        loadDone = false;

    // Колбэк — вызывается когда игрок нажал ИГРАТЬ и загрузка завершена
    std::function<void()> onStartGame;
    // Колбэк — применить настройки окна
    std::function<void(int w, int h, WindowMode, RenderBackend)> onApplySettings;
    GLFWwindow* _window = nullptr;

    // Hover анимации кнопок
    float hoverPlay = 0.f;
    float hoverSettings = 0.f;
    float hoverExit = 0.f;
    float hoverApply = 0.f;
    float hoverBack = 0.f;

    // ─────────────────────────────────────────────────────────
    void init() {
        // Читаем текущие настройки
        apiIndex = (gRenderBackend == RenderBackend::DX11) ? 0 : 1;
        taaEnabled = gTAAEnabled;
        fadingIn = true;
        fadeAlpha = 1.f;
    }

    // ─────────────────────────────────────────────────────────
    void draw(GLFWwindow* window, float dt) {
        time += dt;

        // Fade in/out
        if (fadingIn) {
            fadeAlpha -= dt * 1.5f;
            if (fadeAlpha <= 0.f) { fadeAlpha = 0.f; fadingIn = false; }
        }
        if (fadingOut) {
            fadeAlpha += dt * 1.2f;
            if (fadeAlpha >= 1.f) {
                fadeAlpha = 1.f;
                fadingOut = false;
                state = MenuState::LOADING;
                loadProgress = 0.f;
                loadMsgIdx = 0;
                loadMsgTimer = 0.f;
                loadDone = false;
            }
        }

        // Новый ImGui кадр
        if (gRenderBackend == RenderBackend::DX11) ImGui_ImplDX11_NewFrame();
        else ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // SCR_WIDTH/HEIGHT обновляются renderer.resize() при смене режима
        float fw = (float)SCR_WIDTH, fh = (float)SCR_HEIGHT;
        ImGui::GetIO().DisplaySize = ImVec2(fw, fh);
        ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.f, 1.f);

        // Полноэкранное невидимое окно — фон
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ fw, fh });
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin("##bg", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Фон — тёмный градиент с зернистостью ───────────────
        dl->AddRectFilledMultiColor({ 0,0 }, { fw,fh },
            IM_COL32(8, 10, 12, 255),
            IM_COL32(12, 14, 18, 255),
            IM_COL32(5, 7, 10, 255),
            IM_COL32(10, 12, 15, 255));

        // Сетка — тонкие линии
        float gridAlpha = 18;
        for (float x = 0; x < fw; x += 60.f)
            dl->AddLine({ x, 0 }, { x, fh }, IM_COL32(255, 255, 255, (int)gridAlpha));
        for (float y = 0; y < fh; y += 60.f)
            dl->AddLine({ 0, y }, { fw, y }, IM_COL32(255, 255, 255, (int)gridAlpha));

        // Диагональные акцентные линии
        float lOff = fmodf(time * 30.f, 120.f);
        for (float x = -fh + lOff; x < fw; x += 120.f) {
            dl->AddLine({ x, 0 }, { x + fh, fh },
                IM_COL32(180, 60, 30, 12), 1.f);
        }

        // Угловые декоративные уголки
        auto drawCorner = [&](ImVec2 p, float sx, float sy, float sz = 40.f) {
            ImU32 col = IM_COL32(200, 70, 30, 160);
            dl->AddLine(p, { p.x + sx * sz, p.y }, col, 2.f);
            dl->AddLine(p, { p.x, p.y + sy * sz }, col, 2.f);
            dl->AddLine({ p.x + sx * 4, p.y }, { p.x + sx * 4, p.y + sy * 8 }, col, 1.f);
            dl->AddLine({ p.x, p.y + sy * 4 }, { p.x + sx * 8, p.y + sy * 4 }, col, 1.f);
            };
        drawCorner({ 20, 20 }, 1, 1);
        drawCorner({ fw - 20, 20 }, -1, 1);
        drawCorner({ 20, fh - 20 }, 1, -1);
        drawCorner({ fw - 20, fh - 20 }, -1, -1);

        // Пульсирующий акцент внизу
        float pulse = 0.5f + 0.5f * sinf(time * 2.f);
        dl->AddRectFilled({ 0, fh - 3 }, { fw * pulse * 0.6f, fh },
            IM_COL32(200, 70, 30, (int)(120 * pulse)));

        ImGui::End();

        if (state == MenuState::MAIN)     _drawMain(dl, fw, fh, dt);
        else if (state == MenuState::SETTINGS) _drawSettings(dl, fw, fh, dt);
        else if (state == MenuState::LOADING)  _drawLoading(dl, fw, fh, dt);

        // Fade overlay
        if (fadeAlpha > 0.001f) {
            ImDrawList* fdl = ImGui::GetForegroundDrawList();
            fdl->AddRectFilled({ 0,0 }, { fw,fh },
                IM_COL32(0, 0, 0, (int)(fadeAlpha * 255)));
        }

        ImGui::Render();
        if (gRenderBackend == RenderBackend::DX11) ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        else ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

private:
    // ─────────────────────────────────────────────────────────
    //  Главное меню
    // ─────────────────────────────────────────────────────────
    void _drawMain(ImDrawList* dl, float fw, float fh, float dt) {

        // Заголовок
        float titleY = fh * 0.18f;
        _drawTitle(dl, fw, fh, titleY);

        // Версия
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        fdl->AddText({ fw - 120, fh - 24 },
            IM_COL32(120, 120, 120, 180), "build 0.1");

        // Кнопки
        float btnW = 280.f, btnH = 52.f;
        float btnX = 80.f;
        float btnY = fh * 0.46f;
        float gap = 18.f;

        bool clickPlay = _menuBtn(fdl, ">>  PLAY", { btnX, btnY }, { btnW, btnH }, hoverPlay, dt, IM_COL32(200, 70, 30, 255));
        bool clickSettings = _menuBtn(fdl, "*  SETTINGS", { btnX, btnY + btnH + gap }, { btnW, btnH }, hoverSettings, dt, IM_COL32(120, 120, 130, 255));
        bool clickExit = _menuBtn(fdl, "X  EXIT", { btnX, btnY + (btnH + gap) * 2 }, { btnW, btnH }, hoverExit, dt, IM_COL32(100, 40, 40, 255));

        if (clickPlay && !fadingOut && !fadingIn) {
            fadingOut = true;
        }
        if (clickSettings && !fadingOut && !fadingIn) {
            state = MenuState::SETTINGS;
            fadingIn = true; fadeAlpha = 0.4f;
        }
        if (clickExit) {
            exit(0);
        }

        // Подсказка внизу
        fdl->AddText({ btnX, fh - 40 },
            IM_COL32(80, 80, 80, 200),
            "WASD + MOUSE");
    }

    // ─────────────────────────────────────────────────────────
    //  Настройки
    // ─────────────────────────────────────────────────────────
    void _drawSettings(ImDrawList* dl, float fw, float fh, float dt) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();

        // Заголовок секции
        fdl->AddText(ImGui::GetFont(), 28.f, { 80, 80 },
            IM_COL32(200, 200, 200, 240), "SETTINGS");
        fdl->AddLine({ 80, 115 }, { 400, 115 }, IM_COL32(200, 70, 30, 200), 2.f);

        float lx = 80.f, ly = 140.f, lh = 68.f;

        // ── Graphics API ─────────────────────────────────────
        fdl->AddText({ lx, ly }, IM_COL32(160, 160, 160, 255), "GRAPHICS API");
        float bw = 140.f, bh = 38.f, by = ly + 22.f;
        bool dx11Active = (apiIndex == 0);
        bool oglActive = (apiIndex == 1);

        static float hDX = 0, hOGL = 0;
        bool cd = _tabBtn(fdl, "DirectX 11", { lx,     by }, { bw, bh }, hDX, dt, dx11Active);
        bool co = _tabBtn(fdl, "OpenGL", { lx + bw + 8,by }, { bw, bh }, hOGL, dt, oglActive);
        if (cd) apiIndex = 0;
        if (co) apiIndex = 1;

        ly += lh;

        // ── Разрешение ───────────────────────────────────────
        fdl->AddText({ lx, ly }, IM_COL32(160, 160, 160, 255), "RESOLUTION");
        float ry = ly + 22.f;
        static float hRes[4] = {};
        for (int i = 0; i < kResCount; i++) {
            bool act = (resIndex == i);
            if (_tabBtn(fdl, kResPresets[i].label,
                { lx + i * (bw + 8.f), ry }, { bw, bh }, hRes[i], dt, act))
                resIndex = i;
        }
        ly += lh;

        // ── Режим окна ───────────────────────────────────────
        fdl->AddText({ lx, ly }, IM_COL32(160, 160, 160, 255), "WINDOW MODE");
        float wy = ly + 22.f;
        static float hWin[3] = {};
        const char* winLabels[] = { "Windowed","Borderless","Fullscreen" };
        for (int i = 0; i < 3; i++) {
            bool act = ((int)winMode == i);
            if (_tabBtn(fdl, winLabels[i],
                { lx + i * (bw + 8.f), wy }, { bw, bh }, hWin[i], dt, act))
                winMode = (WindowMode)i;
        }
        ly += lh + 10.f;

        // ── Сглаживание ──────────────────────────────────────
        fdl->AddText({ lx, ly }, IM_COL32(160, 160, 160, 255), "ANTI-ALIASING");
        float ay = ly + 22.f;
        static float hTAAon = 0, hTAAoff = 0;
        if (_tabBtn(fdl, "TAA  ON", { lx,        ay }, { bw,bh }, hTAAon, dt, taaEnabled))  taaEnabled = true;
        if (_tabBtn(fdl, "TAA  OFF", { lx + bw + 8.f,ay }, { bw,bh }, hTAAoff, dt, !taaEnabled)) taaEnabled = false;
        ly += lh;

        // ── Предупреждение о смене API ───────────────────────
        int newApi = apiIndex;
        bool apiWillChange = (newApi == 0) != (gRenderBackend == RenderBackend::DX11);
        if (apiWillChange) {
            fdl->AddRectFilled({ lx - 4, ly - 4 }, { lx + 420, ly + 28 },
                IM_COL32(80, 30, 10, 180), 4.f);
            fdl->AddText({ lx, ly }, IM_COL32(255, 160, 60, 255),
                "!  API change requires restart");
            ly += 36.f;
        }

        // ── Кнопки ───────────────────────────────────────────
        bool apply = _menuBtn(fdl, "OK  APPLY", { lx, ly }, { 200, 44 }, hoverApply, dt, IM_COL32(200, 70, 30, 255));
        bool back = _menuBtn(fdl, "<  BACK", { lx + 216, ly }, { 160, 44 }, hoverBack, dt, IM_COL32(80, 80, 90, 255));

        if (apply) {
            gTAAEnabled = taaEnabled;  // применяется мгновенно
            gRenderBackend = (apiIndex == 0) ? RenderBackend::DX11 : RenderBackend::OpenGL;
            saveEngineConfig();
            auto& rp = kResPresets[resIndex];
            if (onApplySettings)
                onApplySettings(rp.w, rp.h, winMode, gRenderBackend);
            settingsChanged = true;
        }
        if (back) {
            state = MenuState::MAIN;
            fadingIn = true; fadeAlpha = 0.4f;
        }
    }

    // ─────────────────────────────────────────────────────────
    //  Экран загрузки
    // ─────────────────────────────────────────────────────────
    void _drawLoading(ImDrawList* dl, float fw, float fh, float dt) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();

        // Прогресс
        loadMsgTimer += dt;
        float msgDur = 0.45f;
        if (loadMsgTimer >= msgDur && loadMsgIdx < kLoadMsgCount - 1) {
            loadMsgTimer = 0.f;
            loadMsgIdx++;
            loadProgress = (float)loadMsgIdx / (kLoadMsgCount - 1);
        }
        if (loadMsgIdx >= kLoadMsgCount - 1 && !loadDone) {
            loadProgress += dt * 0.8f;
            if (loadProgress >= 1.f) {
                loadProgress = 1.f;
                loadDone = true;
                // Небольшая задержка потом запускаем игру
            }
        }

        // Запуск игры после завершения загрузки
        static float doneTimer = 0.f;
        if (loadDone) {
            doneTimer += dt;
            if (doneTimer > 0.6f) {
                doneTimer = 0.f;
                state = MenuState::INGAME;
                fadingIn = true; fadeAlpha = 1.f;
                if (onStartGame) onStartGame();
            }
        }

        // Центр
        float cx = fw * 0.5f, cy = fh * 0.5f;

        // Крутящийся спиннер
        int segments = 12;
        float radius = 38.f;
        for (int i = 0; i < segments; i++) {
            float angle = time * 3.f + i * (2.f * 3.14159f / segments);
            float alpha = (float)i / segments;
            float x1 = cx + cosf(angle) * (radius - 8);
            float y1 = cy - 80 + sinf(angle) * (radius - 8);
            float x2 = cx + cosf(angle) * radius;
            float y2 = cy - 80 + sinf(angle) * radius;
            dl->AddLine({ x1,y1 }, { x2,y2 },
                IM_COL32(200, 70, 30, (int)(alpha * 255)), 3.f);
        }

        // Внешний декоративный круг
        dl->AddCircle({ cx, cy - 80 }, radius + 12, IM_COL32(200, 70, 30, 30), 64, 1.5f);

        // Текст ЗАГРУЗКА
        const char* loadTxt = "P.L.A.Y.B.A.C.K.";
        ImVec2 tsz = ImGui::CalcTextSize(loadTxt);
        fdl->AddText(ImGui::GetFont(), 22.f,
            { cx - tsz.x * 0.6f, cy - 20 },
            IM_COL32(200, 200, 200, 220), loadTxt);

        // Текущее сообщение
        const char* msg = kLoadMsgs[loadMsgIdx];
        ImVec2 msz = ImGui::CalcTextSize(msg);
        float msgAlpha = 180.f + 60.f * sinf(time * 4.f);
        fdl->AddText({ cx - msz.x * 0.5f, cy + 14 },
            IM_COL32(140, 140, 140, (int)msgAlpha), msg);

        // Прогресс бар
        float barW = 400.f, barH = 4.f;
        float barX = cx - barW * 0.5f, barY = cy + 50.f;
        // Фон
        fdl->AddRectFilled({ barX, barY }, { barX + barW, barY + barH },
            IM_COL32(40, 40, 40, 255), 2.f);
        // Заполнение
        float filled = barW * loadProgress;
        if (filled > 0) {
            fdl->AddRectFilledMultiColor(
                { barX, barY }, { barX + filled, barY + barH },
                IM_COL32(150, 40, 10, 255), IM_COL32(220, 80, 30, 255),
                IM_COL32(220, 80, 30, 255), IM_COL32(150, 40, 10, 255));
            // Блик на конце
            fdl->AddRectFilled(
                { barX + filled - 4, barY - 1 },
                { barX + filled + 2, barY + barH + 1 },
                IM_COL32(255, 180, 100, 200), 1.f);
        }
        // Процент
        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(loadProgress * 100));
        fdl->AddText({ barX + barW + 10, barY - 4 },
            IM_COL32(180, 180, 180, 200), pctBuf);

        // Если готово — ГОТОВО
        if (loadDone) {
            float pulse2 = 0.7f + 0.3f * sinf(time * 6.f);
            const char* doneTxt = "READY";
            ImVec2 dsz2 = ImGui::CalcTextSize(doneTxt);
            fdl->AddText(ImGui::GetFont(), 20.f,
                { cx - dsz2.x * 0.55f, cy + 70 },
                IM_COL32(200, 220, 180, (int)(pulse2 * 220)), doneTxt);
        }
    }

    // ─────────────────────────────────────────────────────────
    //  Рисуем большой заголовок
    // ─────────────────────────────────────────────────────────
    void _drawTitle(ImDrawList* dl, float fw, float fh, float titleY) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();

        // Свечение за заголовком
        float glow = 0.3f + 0.1f * sinf(time * 1.5f);
        fdl->AddRectFilled({ 60, titleY - 20 }, { 500, titleY + 90 },
            IM_COL32(200, 60, 20, (int)(glow * 40)), 8.f);

        // Основной текст
        fdl->AddText(ImGui::GetFont(), 64.f, { 80, titleY },
            IM_COL32(240, 240, 240, 255), "P.L.A.Y.B.A.C.K.");

        // Подзаголовок
        fdl->AddText(ImGui::GetFont(), 16.f, { 84, titleY + 72 },
            IM_COL32(200, 70, 30, 220), "early access build");

        // Линия под заголовком
        fdl->AddLine({ 80, titleY + 95 }, { 460, titleY + 95 },
            IM_COL32(200, 70, 30, 180), 1.5f);
        fdl->AddLine({ 80, titleY + 98 }, { 200, titleY + 98 },
            IM_COL32(200, 70, 30, 80), 1.f);
    }

    // ─────────────────────────────────────────────────────────
    //  Кнопка меню с hover анимацией
    // ─────────────────────────────────────────────────────────
    bool _menuBtn(ImDrawList* dl, const char* label,
        ImVec2 pos, ImVec2 size,
        float& hover, float dt, ImU32 accentCol)
    {
        ImVec2 p1 = pos, p2 = { pos.x + size.x, pos.y + size.y };
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool hovered = mp.x >= p1.x && mp.x <= p2.x &&
            mp.y >= p1.y && mp.y <= p2.y;
        bool clicked = hovered && ImGui::IsMouseClicked(0);

        hover += dt * (hovered ? 6.f : -6.f);
        hover = glm::clamp(hover, 0.f, 1.f);

        // Фон кнопки
        ImU32 bgCol = IM_COL32(
            (int)(15 + hover * 18),
            (int)(15 + hover * 18),
            (int)(18 + hover * 18),
            (int)(180 + hover * 60));
        dl->AddRectFilled(p1, p2, bgCol, 3.f);

        // Левая акцентная полоска
        float barH = size.y * (0.3f + hover * 0.7f);
        float barY = pos.y + (size.y - barH) * 0.5f;
        dl->AddRectFilled(
            { pos.x, barY },
            { pos.x + 3.f, barY + barH },
            accentCol);

        // Рамка при hover
        if (hover > 0.01f) {
            dl->AddRect(p1, p2,
                IM_COL32(
                    (accentCol >> 0) & 0xFF,
                    (accentCol >> 8) & 0xFF,
                    (accentCol >> 16) & 0xFF,
                    (int)(hover * 80)),
                3.f, 0, 1.f);
        }

        // Текст
        ImVec2 tsz = ImGui::CalcTextSize(label);
        float tx = pos.x + 18.f;
        float ty = pos.y + (size.y - tsz.y) * 0.5f;
        dl->AddText({ tx, ty },
            IM_COL32(200 + (int)(hover * 55),
                200 + (int)(hover * 55),
                200 + (int)(hover * 55), 255),
            label);

        // Стрелка при hover
        if (hover > 0.1f) {
            float ax = p2.x - 20.f;
            float ay = pos.y + size.y * 0.5f;
            dl->AddTriangleFilled(
                { ax,      ay - 5.f },
                { ax + 8,  ay },
                { ax,      ay + 5.f },
                IM_COL32(
                    (accentCol >> 0) & 0xFF,
                    (accentCol >> 8) & 0xFF,
                    (accentCol >> 16) & 0xFF,
                    (int)(hover * 200)));
        }

        return clicked;
    }

    // ─────────────────────────────────────────────────────────
    //  Tab-кнопка для настроек
    // ─────────────────────────────────────────────────────────
    bool _tabBtn(ImDrawList* dl, const char* label,
        ImVec2 pos, ImVec2 size,
        float& hover, float dt, bool active)
    {
        ImVec2 p1 = pos, p2 = { pos.x + size.x, pos.y + size.y };
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool hovered = mp.x >= p1.x && mp.x <= p2.x &&
            mp.y >= p1.y && mp.y <= p2.y;
        bool clicked = hovered && ImGui::IsMouseClicked(0);

        hover += dt * (hovered ? 8.f : -8.f);
        hover = glm::clamp(hover, 0.f, 1.f);

        ImU32 bg = active
            ? IM_COL32(200, 70, 30, 200)
            : IM_COL32(30, 30, 35, (int)(180 + hover * 50));
        dl->AddRectFilled(p1, p2, bg, 3.f);
        dl->AddRect(p1, p2,
            active ? IM_COL32(220, 100, 50, 180) : IM_COL32(60, 60, 70, (int)(hover * 150)),
            3.f, 0, 1.f);

        ImVec2 tsz = ImGui::CalcTextSize(label);
        dl->AddText(
            { pos.x + (size.x - tsz.x) * 0.5f, pos.y + (size.y - tsz.y) * 0.5f },
            active ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, (int)(200 + hover * 55)),
            label);

        return clicked;
    }
};

inline MainMenu gMenu;