#pragma once
// ════════════════════════════════════════════════════════════
//  Hud.h  —  игровой HUD через Dear ImGui
//  Работает одинаково на OpenGL и DX11
// ════════════════════════════════════════════════════════════
#include "Settings.h"
#include "Player.h"
#include "WeaponManager.h"
#include <imgui.h>
#include <sstream>
#include <string>
#include <cmath>

// ─── внешние переменные (объявлены в других .h / main.cpp) ──
extern Character    player;
extern float        playerHP;
extern float        playerMaxHP;
extern GunState     gun;

// ════════════════════════════════════════════════════════════
//  Вспомогалки
// ════════════════════════════════════════════════════════════
static inline ImVec4 lerpColor(ImVec4 a, ImVec4 b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
             a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
}

// ════════════════════════════════════════════════════════════
//  Главная функция HUD
// ════════════════════════════════════════════════════════════
inline void drawHUD(float dt, float fpsValue, bool showFPS,
    bool showPos, bool noclip, bool godMode,
    float reloadTimer = 0.f, float reloadDuration = 2.5f)
{
    ImGuiIO& io = ImGui::GetIO();
    float    W = io.DisplaySize.x;
    float    H = io.DisplaySize.y;
    float    T = (float)ImGui::GetTime();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ════════════════════════════════════════════════════════
    //  ПРИЦЕЛ — крест + точка в центре
    // ════════════════════════════════════════════════════════
    float cx = W * 0.5f, cy = H * 0.5f;
    float gap = 5.f, len = 9.f, thick = 1.5f;
    ImU32 chCol = IM_COL32(255, 255, 255, 210);
    ImU32 chSh = IM_COL32(0, 0, 0, 120);

    // Тень
    dl->AddLine({ cx - gap - len + 1, cy + 1 }, { cx - gap + 1, cy + 1 }, chSh, thick);
    dl->AddLine({ cx + gap + 1,       cy + 1 }, { cx + gap + len + 1, cy + 1 }, chSh, thick);
    dl->AddLine({ cx + 1, cy - gap - len + 1 }, { cx + 1, cy - gap + 1 }, chSh, thick);
    dl->AddLine({ cx + 1, cy + gap + 1 }, { cx + 1, cy + gap + len + 1 }, chSh, thick);
    // Крест
    dl->AddLine({ cx - gap - len, cy }, { cx - gap, cy }, chCol, thick);
    dl->AddLine({ cx + gap,       cy }, { cx + gap + len, cy }, chCol, thick);
    dl->AddLine({ cx, cy - gap - len }, { cx, cy - gap }, chCol, thick);
    dl->AddLine({ cx, cy + gap }, { cx, cy + gap + len }, chCol, thick);
    // Центральная точка
    dl->AddCircleFilled({ cx, cy }, 1.5f, chCol);

    // ════════════════════════════════════════════════════════
    //  ВИНЬЕТКА при низком HP
    // ════════════════════════════════════════════════════════
    float hpFrac = playerMaxHP > 0.f
        ? std::max(0.f, std::min(1.f, playerHP / playerMaxHP)) : 1.f;

    if (hpFrac < 0.35f) {
        float pulse = 0.50f + 0.50f * sinf(T * 2.8f);
        float a = ((0.35f - hpFrac) / 0.35f) * 0.55f * pulse;
        ImU32 vCol = IM_COL32(200, 20, 20, (int)(a * 255));
        float vw = W * 0.18f, vh = H * 0.20f;
        // 4 стороны
        dl->AddRectFilled({ 0,     0 }, { vw, H }, vCol);
        dl->AddRectFilled({ W - vw,  0 }, { W,  H }, vCol);
        dl->AddRectFilled({ 0,     0 }, { W,  vh }, IM_COL32(200, 20, 20, (int)(a * 0.6f * 255)));
        dl->AddRectFilled({ 0,     H - vh }, { W,  H }, IM_COL32(200, 20, 20, (int)(a * 0.6f * 255)));
    }

    // ════════════════════════════════════════════════════════
    //  HP — левый нижний угол
    // ════════════════════════════════════════════════════════
    {
        float hx = 24.f, hy = H - 74.f;
        float bW = 175.f, bH = 6.f;

        // Подпись
        float hpAlpha = (hpFrac < 0.25f)
            ? 0.55f + 0.45f * sinf(T * 5.5f) : 0.70f;

        ImVec4 hpColV;
        if (hpFrac > 0.5f)
            hpColV = lerpColor({ 0.85f,0.75f,0.10f,1 }, { 0.20f,0.82f,0.35f,1 }, (hpFrac - 0.5f) * 2.f);
        else
            hpColV = lerpColor({ 0.88f,0.12f,0.10f,1 }, { 0.85f,0.75f,0.10f,1 }, hpFrac * 2.f);
        ImU32 hpCol32 = IM_COL32(
            (int)(hpColV.x * 255), (int)(hpColV.y * 255),
            (int)(hpColV.z * 255), (int)(hpAlpha * 255));

        ImFont* font = ImGui::GetFont();
        float fs = ImGui::GetFontSize();

        // Метка "HEALTH"
        dl->AddText(font, fs * 0.72f, { hx, hy },
            IM_COL32(160, 155, 140, (int)(0.70f * 255)), "HEALTH");

        // Фон бара
        dl->AddRectFilled({ hx - 1, hy + fs * 0.72f + 4 },
            { hx + bW + 1, hy + fs * 0.72f + 4 + bH + 2 },
            IM_COL32(0, 0, 0, 165), 3.f);
        dl->AddRectFilled({ hx, hy + fs * 0.72f + 5 },
            { hx + bW, hy + fs * 0.72f + 5 + bH },
            IM_COL32(22, 22, 20, 240), 2.f);

        // Заполнение
        if (hpFrac > 0.f)
            dl->AddRectFilled({ hx, hy + fs * 0.72f + 5 },
                { hx + bW * hpFrac, hy + fs * 0.72f + 5 + bH },
                hpCol32, 2.f);
        // Блик
        dl->AddRectFilled({ hx, hy + fs * 0.72f + 5 },
            { hx + bW * hpFrac, hy + fs * 0.72f + 5 + bH * 0.4f },
            IM_COL32(255, 255, 255, 18));

        // Цифры
        std::string hpStr = std::to_string((int)playerHP)
            + " / "
            + std::to_string((int)playerMaxHP);
        dl->AddText(font, fs, { hx, hy + fs * 0.72f + 13 },
            hpCol32, hpStr.c_str());
    }

    // ════════════════════════════════════════════════════════
    //  ОРУЖИЕ — правый нижний угол
    // ════════════════════════════════════════════════════════
    {
        const auto& def = weaponManager.activeDef();
        float pW = 210.f, pH = 66.f;
        float wx = W - pW - 14.f, wy = H - pH - 14.f;

        // Панель
        dl->AddRectFilled({ wx - 10, wy - 6 }, { wx + pW + 4, wy + pH + 6 },
            IM_COL32(0, 0, 0, 75), 5.f);
        // Акцентная линия сверху
        dl->AddLine({ wx - 10, wy - 6 }, { wx + pW + 4, wy - 6 },
            IM_COL32(168, 158, 128, 110), 1.5f);

        ImFont* font = ImGui::GetFont();
        float fs = ImGui::GetFontSize();

        // Название оружия
        std::string wname = def.file.substr(def.file.rfind('/') + 1);
        wname = wname.substr(0, wname.rfind('.'));
        for (auto& c : wname) c = (char)toupper((unsigned char)c);
        dl->AddText(font, fs * 0.85f, { wx, wy + 4 },
            IM_COL32(162, 155, 138, 195), wname.c_str());

        // Цвет патронов
        ImU32 amCol;
        if (gun.ammo == 0) amCol = IM_COL32(235, 38, 25, 255);
        else if (gun.ammo <= 5) amCol = IM_COL32(235, 178, 25, 255);
        else                    amCol = IM_COL32(235, 230, 215, 255);

        // Основное число патронов (крупно)
        std::string amMain = std::to_string(gun.ammo);
        std::string amRes = " / " + std::to_string(def.maxAmmo);
        dl->AddText(font, fs * 1.25f, { wx, wy + 22 }, amCol, amMain.c_str());
        float amW = ImGui::CalcTextSize(amMain.c_str()).x * 1.25f;
        dl->AddText(font, fs * 0.85f, { wx + amW + 2, wy + 27 },
            IM_COL32(100, 95, 88, 195), amRes.c_str());

        // Мини-бар патронов
        float amFrac = def.maxAmmo > 0
            ? std::max(0.f, std::min(1.f, (float)gun.ammo / def.maxAmmo)) : 0.f;
        dl->AddRectFilled({ wx, wy + 48 }, { wx + pW, wy + 52 },
            IM_COL32(22, 22, 20, 220), 2.f);
        if (amFrac > 0.f)
            dl->AddRectFilled({ wx, wy + 48 }, { wx + pW * amFrac, wy + 52 },
                IM_COL32((int)(amCol >> 0 & 0xFF), (int)(amCol >> 8 & 0xFF),
                    (int)(amCol >> 16 & 0xFF), 210), 2.f);
    }

    // ════════════════════════════════════════════════════════
    //  ПЕРЕЗАРЯДКА — по центру
    // ════════════════════════════════════════════════════════
    if (gun.reloading) {
        float rdur = reloadDuration > 0.f ? reloadDuration : 2.5f;
        float rprog = std::max(0.f, std::min(1.f, reloadTimer / rdur));
        float rW = 140.f, rH = 4.f;
        float rX = cx - rW * 0.5f, rY = cy + 22.f;

        dl->AddRectFilled({ rX - 1, rY - 1 }, { rX + rW + 1, rY + rH + 1 },
            IM_COL32(0, 0, 0, 130), 3.f);
        dl->AddRectFilled({ rX, rY }, { rX + rW, rY + rH },
            IM_COL32(22, 22, 20, 220), 2.f);
        dl->AddRectFilled({ rX, rY }, { rX + rW * rprog, rY + rH },
            IM_COL32(210, 178, 70, 255), 2.f);

        float alpha = 0.60f + 0.40f * sinf(T * 7.f);
        const char* rtext = "RELOADING";
        ImVec2 tsz = ImGui::CalcTextSize(rtext);
        dl->AddText({ cx - tsz.x * 0.5f, rY + rH + 5 },
            IM_COL32(210, 178, 70, (int)(alpha * 255)), rtext);
    }

    // ════════════════════════════════════════════════════════
    //  FPS / позиция / режимы — левый верхний угол
    // ════════════════════════════════════════════════════════
    float sy = 10.f;
    if (showFPS) {
        ImU32 fc;
        if (fpsValue >= 60) fc = IM_COL32(75, 230, 100, 255);
        else if (fpsValue >= 30) fc = IM_COL32(230, 205, 50, 255);
        else                     fc = IM_COL32(230, 50, 50, 255);
        char buf[32]; snprintf(buf, sizeof(buf), "FPS: %.0f", fpsValue);
        ImVec2 tsz = ImGui::CalcTextSize(buf);
        dl->AddText({ W - tsz.x - 10, sy }, fc, buf);
    }
    if (showPos) {
        char buf[64];
        snprintf(buf, sizeof(buf), "X:%.1f Y:%.1f Z:%.1f",
            player.pos.x, player.pos.y, player.pos.z);
        dl->AddText({ 10, sy }, IM_COL32(100, 230, 230, 255), buf);
        sy += ImGui::GetFontSize() + 3;
    }
    if (noclip) {
        dl->AddText({ 10, sy }, IM_COL32(230, 205, 50, 255), "[NOCLIP]");
        sy += ImGui::GetFontSize() + 3;
    }
    if (godMode) {
        dl->AddText({ 10, sy }, IM_COL32(75, 230, 100, 255), "[GOD MODE]");
    }
}