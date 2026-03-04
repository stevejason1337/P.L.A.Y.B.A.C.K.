#pragma once
#include "Settings.h"
#include "TextRenderer.h"
#include "Player.h"
#include "Weaponmanager.h"
#include <GLFW/glfw3.h>
#include <sstream>
#include <string>
#include <cmath>

// Объявления внешних объектов
extern TextRenderer textRenderer;

// ─────────────────────────────────────────────────────────
inline void drawHUD(float dt, float fpsValue, bool showFPS,
    bool showPos, bool noclip, bool godMode,
    float reloadTimer = 0.f, float reloadDuration = 2.5f)
{
    float W = (float)SCR_WIDTH;
    float H = (float)SCR_HEIGHT;
    float T = (float)glfwGetTime();

    // ── FPS / отладка ────────────────────────────────────
    if (showFPS) {
        glm::vec4 fc = fpsValue >= 60
            ? glm::vec4(0.3f, 0.9f, 0.4f, 1)
            : fpsValue >= 30
            ? glm::vec4(0.9f, 0.8f, 0.2f, 1)
            : glm::vec4(0.9f, 0.2f, 0.2f, 1);
        std::ostringstream o;
        o << std::fixed << std::setprecision(0) << "FPS: " << fpsValue;
        textRenderer.drawText(o.str(), W - 90.f, 20.f, fc);
    }
    if (showPos) {
        std::ostringstream o;
        o << std::fixed << std::setprecision(1)
            << "X:" << player.pos.x
            << " Y:" << player.pos.y
            << " Z:" << player.pos.z;
        textRenderer.drawText(o.str(), 8.f, 20.f,
            glm::vec4(0.4f, 0.9f, 0.9f, 1));
    }
    float sy = 38.f;
    if (noclip) {
        textRenderer.drawText("[NOCLIP]", 8.f, sy,
            glm::vec4(0.9f, 0.8f, 0.2f, 1)); sy += 18.f;
    }
    if (godMode) {
        textRenderer.drawText("[GOD]", 8.f, sy,
            glm::vec4(0.3f, 0.9f, 0.4f, 1));
    }

    // ══════════════════════════════════════════════════════
    // ПРИЦЕЛ — маленькая круглая точка (имитируем кружком
    // из нескольких пикселей через маленький rect)
    // ══════════════════════════════════════════════════════
    float cx = W * 0.5f, cy = H * 0.5f;
    float r = 2.5f; // радиус точки

    // Тень
    textRenderer.drawRect(cx - r - 0.5f, cy - r - 0.5f,
        (r * 2) + 1, (r * 2) + 1, glm::vec4(0, 0, 0, 0.5f));
    // Точка (квадратик 5x5 — выглядит как точка при малом размере)
    textRenderer.drawRect(cx - r, cy - r,
        r * 2, r * 2, glm::vec4(1, 1, 1, 0.85f));

    // ══════════════════════════════════════════════════════
    // HP — левый нижний угол
    // ══════════════════════════════════════════════════════
    float hpFrac = playerMaxHP > 0.f ? playerHP / playerMaxHP : 1.f;
    if (hpFrac < 0.f) hpFrac = 0.f;
    if (hpFrac > 1.f) hpFrac = 1.f;

    float hx = 24.f, hy = H - 72.f;
    float bW = 170.f, bH = 5.f;

    // Метка
    textRenderer.drawText("HEALTH", hx, hy,
        glm::vec4(0.45f, 0.44f, 0.40f, 0.75f));

    // Фон бара
    textRenderer.drawRect(hx - 1, hy + 16.f - 1, bW + 2, bH + 2,
        glm::vec4(0, 0, 0, 0.65f));
    textRenderer.drawRect(hx, hy + 16.f, bW, bH,
        glm::vec4(0.10f, 0.10f, 0.09f, 0.95f));

    // Цвет: зелёный → жёлтый → красный
    glm::vec3 hpCol;
    if (hpFrac > 0.5f)
        hpCol = glm::mix(glm::vec3(0.85f, 0.75f, 0.1f),
            glm::vec3(0.2f, 0.82f, 0.35f),
            (hpFrac - 0.5f) * 2.f);
    else
        hpCol = glm::mix(glm::vec3(0.88f, 0.12f, 0.10f),
            glm::vec3(0.85f, 0.75f, 0.1f),
            hpFrac * 2.f);

    // Заполнение
    if (hpFrac > 0.f)
        textRenderer.drawRect(hx, hy + 16.f, bW * hpFrac, bH,
            glm::vec4(hpCol, 1.f));
    // Блик
    textRenderer.drawRect(hx, hy + 16.f, bW * hpFrac, bH * 0.4f,
        glm::vec4(1, 1, 1, 0.07f));

    // Цифры HP
    float hpAlpha = hpFrac < 0.25f
        ? 0.6f + 0.4f * sinf(T * 5.5f) : 1.f;
    glm::vec4 hpTextCol = hpFrac < 0.25f
        ? glm::vec4(1.f, 0.18f, 0.12f, hpAlpha)
        : glm::vec4(hpCol, hpAlpha);
    std::ostringstream hpSS;
    hpSS << (int)playerHP << " / " << (int)playerMaxHP;
    textRenderer.drawText(hpSS.str(), hx, hy + 30.f, hpTextCol);

    // ══════════════════════════════════════════════════════
    // ОРУЖИЕ — правый нижний угол
    // ══════════════════════════════════════════════════════
    const auto& def = weaponManager.activeDef();

    float wx = W - 215.f, wy = H - 78.f;
    float pW = 200.f, pH = 62.f;

    // Полупрозрачная панель
    textRenderer.drawRect(wx - 10, wy - 6, pW + 14, pH + 8,
        glm::vec4(0.f, 0.f, 0.f, 0.32f));
    // Акцентная линия сверху
    textRenderer.drawRect(wx - 10, wy - 6, pW + 14, 1.5f,
        glm::vec4(0.68f, 0.62f, 0.50f, 0.55f));

    // Название оружия заглавными
    std::string wname = def.file.substr(def.file.rfind('/') + 1);
    wname = wname.substr(0, wname.rfind('.'));
    for (auto& c : wname) c = (char)toupper((unsigned char)c);
    textRenderer.drawText(wname, wx, wy + 4.f,
        glm::vec4(0.65f, 0.62f, 0.55f, 0.80f));

    // Патроны
    std::string amMain = std::to_string(gun.ammo);
    std::string amRes = " / " + std::to_string(def.maxAmmo);

    glm::vec4 amCol = gun.ammo == 0
        ? glm::vec4(0.92f, 0.15f, 0.10f, 1.f)
        : gun.ammo <= 5
        ? glm::vec4(0.92f, 0.70f, 0.10f, 1.f)
        : glm::vec4(0.92f, 0.90f, 0.84f, 1.f);

    float ax = textRenderer.drawText(amMain, wx, wy + 26.f, amCol);
    textRenderer.drawText(amRes, ax, wy + 26.f,
        glm::vec4(0.40f, 0.38f, 0.35f, 0.80f));

    // Мини-бар патронов
    float amFrac = def.maxAmmo > 0
        ? (float)gun.ammo / def.maxAmmo : 0.f;
    textRenderer.drawRect(wx, wy + 44.f, pW, 3.f,
        glm::vec4(0.10f, 0.10f, 0.09f, 0.90f));
    if (amFrac > 0.f)
        textRenderer.drawRect(wx, wy + 44.f, pW * amFrac, 3.f,
            glm::vec4(amCol.r, amCol.g, amCol.b, 0.85f));

    // ── Перезарядка ──────────────────────────────────────
    if (gun.reloading) {
        float rdur = reloadDuration > 0.f ? reloadDuration : 2.5f;
        float rprog = rdur > 0.f ? (reloadTimer / rdur) : 0.f;
        rprog = rprog < 0.f ? 0.f : rprog > 1.f ? 1.f : rprog;

        float rW = 130.f, rH = 3.f;
        float rX = cx - rW * 0.5f, rY = cy + 20.f;

        textRenderer.drawRect(rX - 1, rY - 1, rW + 2, rH + 2,
            glm::vec4(0, 0, 0, 0.55f));
        textRenderer.drawRect(rX, rY, rW, rH,
            glm::vec4(0.10f, 0.10f, 0.09f, 0.90f));
        textRenderer.drawRect(rX, rY, rW * rprog, rH,
            glm::vec4(0.82f, 0.70f, 0.28f, 1.f));

        float alpha = 0.65f + 0.35f * sinf(T * 7.f);
        float tw = textRenderer.textWidth("RELOADING");
        textRenderer.drawText("RELOADING",
            cx - tw * 0.5f, rY + 15.f,
            glm::vec4(0.82f, 0.70f, 0.28f, alpha));
    }

    // ══════════════════════════════════════════════════════
    // Красная виньетка при низком HP
    // ══════════════════════════════════════════════════════
    if (hpFrac < 0.30f) {
        float a = ((0.30f - hpFrac) / 0.30f) * 0.38f;
        a *= 0.55f + 0.45f * sinf(T * 2.8f);
        float vw = 90.f;
        textRenderer.drawRect(0, 0, vw, H, glm::vec4(0.75f, 0, 0, a));
        textRenderer.drawRect(W - vw, 0, vw, H, glm::vec4(0.75f, 0, 0, a));
        textRenderer.drawRect(0, 0, W, vw, glm::vec4(0.75f, 0, 0, a * 0.6f));
        textRenderer.drawRect(0, H - vw, W, vw, glm::vec4(0.75f, 0, 0, a * 0.6f));
    }
}