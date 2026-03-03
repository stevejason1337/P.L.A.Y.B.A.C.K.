#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include "TextRenderer.h"
#include "Settings.h"
#include "Enemy.h"
#include "WeaponManager.h"

#define CON_WHITE  glm::vec4(0.85f,0.85f,0.85f,1.f)
#define CON_YELLOW glm::vec4(1.f,0.8f,0.2f,1.f)
#define CON_RED    glm::vec4(1.f,0.3f,0.3f,1.f)
#define CON_GREEN  glm::vec4(0.4f,1.f,0.4f,1.f)
#define CON_CYAN   glm::vec4(0.4f,0.9f,1.f,1.f)
#define CON_GRAY   glm::vec4(0.55f,0.55f,0.55f,1.f)

inline bool  noclip = false;
inline bool  godMode = false;
inline bool  showFPS = true;
inline bool  showPos = true;
inline bool  showHUD = true;
inline float timeScale = 1.f;
inline bool  wireframe = false;
inline float fpsValue = 0.f;

struct ConLine {
    std::string text;
    glm::vec4   color;
};

// externals
extern Character player;
extern GunState  gun;
extern float     playerHP;
extern float     playerMaxHP;

struct Console
{
    // Публичные цвета для использования снаружи
    const glm::vec4 CON_CYAN_VAL = CON_CYAN;
    const glm::vec4 CON_WHITE_VAL = CON_WHITE;

    bool  open = false;
    bool  justOpened = false;

    std::string          input;
    std::vector<ConLine> lines;
    std::vector<std::string> history;
    int   histIdx = -1;
    int   scroll = 0;
    float cursorBlink = 0.f;
    bool  cursorVis = true;

    float conH()  const { return SCR_HEIGHT * 0.5f; }
    float conW()  const { return (float)SCR_WIDTH; }
    float lineH() const { return 18.f; }
    float padX()  const { return 8.f; }

    void init()
    {
        print("=== 3D Engine Developer Console ===", CON_YELLOW);
        print("Type 'help' for commands.", CON_GRAY);
        print("", CON_WHITE);
    }

    void print(const std::string& text, glm::vec4 col = CON_WHITE)
    {
        lines.push_back({ text, col });
        if ((int)lines.size() > 512) lines.erase(lines.begin());
        scroll = 0;
    }

    void toggle()
    {
        open = !open;
        justOpened = open;
        if (open) { input = ""; histIdx = -1; }
    }

    void execute(const std::string& cmd)
    {
        if (cmd.empty()) return;
        history.push_back(cmd);
        histIdx = -1;
        print("] " + cmd, CON_WHITE);

        std::istringstream ss(cmd);
        std::string name; ss >> name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        if (name == "help") {
            print("--- General ---", CON_YELLOW);
            print("noclip              - fly through walls", CON_CYAN);
            print("god                 - god mode", CON_CYAN);
            print("kill                - respawn", CON_CYAN);
            print("give ammo           - refill ammo", CON_CYAN);
            print("give weapon <N>     - switch weapon", CON_CYAN);
            print("setpos <x> <y> <z>  - teleport", CON_CYAN);
            print("timescale <f>       - time scale", CON_CYAN);
            print("--- Display ---", CON_YELLOW);
            print("fps / pos / hud     - toggle displays", CON_CYAN);
            print("wireframe           - toggle wireframe", CON_CYAN);
            print("--- Enemies ---", CON_YELLOW);
            print("spawn [N]           - spawn N enemies near you", CON_CYAN);
            print("killall             - kill all enemies", CON_CYAN);
            print("enemies             - show enemy count", CON_CYAN);
            print("--- Other ---", CON_YELLOW);
            print("clear / quit        - clear / exit", CON_CYAN);
        }
        else if (name == "noclip") {
            noclip = !noclip;
            print(noclip ? "noclip ON" : "noclip OFF", CON_GREEN);
        }
        else if (name == "god") {
            godMode = !godMode;
            print(godMode ? "God mode ON — you cannot die" : "God mode OFF", CON_GREEN);
        }
        else if (name == "kill") {
            player.pos.y = -9999.f;
            playerHP = playerMaxHP;
            print("Killed. Respawning...", CON_RED);
        }
        else if (name == "give") {
            std::string what; ss >> what;
            std::transform(what.begin(), what.end(), what.begin(), ::tolower);
            if (what == "ammo") {
                gun.ammo = weaponManager.activeDef().maxAmmo;
                print("Ammo refilled.", CON_GREEN);
            }
            else if (what == "weapon") {
                int idx = 0; ss >> idx;
                weaponManager.switchTo(idx);
                print("Switched to weapon " + std::to_string(idx), CON_GREEN);
            }
            else { print("give: unknown: " + what, CON_RED); }
        }
        else if (name == "setpos") {
            float x, y, z;
            if (ss >> x >> y >> z) {
                player.pos = glm::vec3(x, y, z);
                player.vel = glm::vec3(0);
                std::ostringstream o;
                o << std::fixed << std::setprecision(1);
                o << "Teleported to (" << x << ", " << y << ", " << z << ")";
                print(o.str(), CON_GREEN);
            }
            else print("Usage: setpos <x> <y> <z>", CON_RED);
        }
        else if (name == "timescale") {
            float v; if (ss >> v) {
                timeScale = glm::clamp(v, 0.01f, 10.f);
                print("timescale = " + std::to_string(timeScale), CON_GREEN);
            }
            else print("Usage: timescale <value>", CON_RED);
        }
        else if (name == "fps") { showFPS = !showFPS; print(showFPS ? "FPS ON" : "FPS OFF", CON_GREEN); }
        else if (name == "pos") { showPos = !showPos; print(showPos ? "Pos ON" : "Pos OFF", CON_GREEN); }
        else if (name == "hud") { showHUD = !showHUD; print(showHUD ? "HUD ON" : "HUD OFF", CON_GREEN); }
        else if (name == "wireframe") {
            wireframe = !wireframe;
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            print(wireframe ? "Wireframe ON" : "Wireframe OFF", CON_GREEN);
        }
        else if (name == "spawn") {
            int n = 1; ss >> n; if (n < 1)n = 1; if (n > 20)n = 20;
            glm::vec3 spawnPos = player.pos + glm::vec3(5, 0, 0);
            enemyManager.spawnGroup(spawnPos, n, n > 1 ? 4.f : 0.f);
            print("Spawned " + std::to_string(n) + " enemy(s).", CON_GREEN);
        }
        else if (name == "killall") {
            int cnt = 0;
            for (auto& e : enemyManager.enemies) {
                if (!e.isDead()) { e.takeDamage(9999.f); cnt++; }
            }
            print("Killed " + std::to_string(cnt) + " enemies.", CON_GREEN);
        }
        else if (name == "enemies") {
            print("Enemies alive: " + std::to_string(enemyManager.enemies.size()), CON_CYAN);
        }
        else if (name == "clear") { lines.clear(); }
        else if (name == "quit" || name == "exit") { print("Goodbye.", CON_YELLOW); exit(0); }
        else { print("Unknown command: " + name + "  (type 'help')", CON_RED); }

        input = "";
    }

    void charInput(unsigned int c)
    {
        if (!open) return;
        if (c >= 32 && c < 127) input += (char)c;
    }

    void keyInput(int key, int action)
    {
        if (!open) return;
        if (action != 1 && action != 2) return;
        if (key == 257) execute(input);
        else if (key == 259) { if (!input.empty()) input.pop_back(); }
        else if (key == 265) {
            if (!history.empty()) {
                histIdx = std::min((int)history.size() - 1, histIdx + 1);
                input = history[history.size() - 1 - histIdx];
            }
        }
        else if (key == 264) {
            if (histIdx > 0) { histIdx--; input = history[history.size() - 1 - histIdx]; }
            else { histIdx = -1; input = ""; }
        }
        else if (key == 258) autoComplete();
        else if (key == 266) scroll = std::min(scroll + 5, (int)lines.size());
        else if (key == 267) scroll = std::max(scroll - 5, 0);
    }

    void autoComplete()
    {
        static const std::vector<std::string> cmds = {
            "help","noclip","god","kill","give ammo","give weapon",
            "setpos","timescale","fps","pos","hud","wireframe",
            "spawn","killall","enemies","clear","quit"
        };
        std::vector<std::string> matches;
        for (auto& c : cmds) if (c.substr(0, input.size()) == input) matches.push_back(c);
        if (matches.size() == 1) input = matches[0];
        else if (matches.size() > 1) { print("", CON_WHITE); for (auto& m : matches) print("  " + m, CON_CYAN); }
    }

    void update(float dt)
    {
        cursorBlink += dt;
        if (cursorBlink > 0.5f) { cursorBlink = 0; cursorVis = !cursorVis; }
    }

    void draw()
    {
        if (!open) return;
        float W = conW(), H = conH(), lh = lineH(), px = padX();

        textRenderer.drawRect(0, 0, W, H, glm::vec4(0.05f, 0.07f, 0.05f, 0.92f));
        textRenderer.drawRect(0, 0, W, 24.f, glm::vec4(0.1f, 0.2f, 0.1f, 1.f));
        textRenderer.drawText("DEVELOPER CONSOLE", px, 17.f, CON_YELLOW);
        std::string ver = "3D Engine v0.1";
        textRenderer.drawText(ver, W - textRenderer.textWidth(ver) - px, 17.f, CON_GRAY);
        textRenderer.drawRect(0, 24.f, W, 1.f, glm::vec4(0.3f, 0.5f, 0.3f, 1.f));

        textRenderer.drawRect(0, H - 24.f, W, 24.f, glm::vec4(0.08f, 0.12f, 0.08f, 1.f));
        textRenderer.drawRect(0, H - 25.f, W, 1.f, glm::vec4(0.3f, 0.5f, 0.3f, 1.f));
        std::string inputLine = "] " + input;
        textRenderer.drawText(inputLine, px, H - 4.f, CON_WHITE);
        if (cursorVis) {
            float cx = textRenderer.textWidth(inputLine) + px + 1.f;
            textRenderer.drawRect(cx, H - 22.f, 8.f, 16.f, glm::vec4(0.8f, 0.9f, 0.8f, 0.9f));
        }

        float areaH = H - 24.f - 26.f;
        int maxLines = (int)(areaH / lh);
        int total = (int)lines.size();
        int startIdx = std::max(0, total - maxLines - scroll);
        int endIdx = std::max(0, total - scroll);
        float ty = 24.f + lh;
        for (int i = startIdx; i < endIdx; i++) {
            textRenderer.drawText(lines[i].text, px, ty, lines[i].color);
            ty += lh;
        }
        if (scroll > 0) {
            std::string hint = "[" + std::to_string(scroll) + " lines up | PgDn]";
            textRenderer.drawText(hint, px, H - 42.f, CON_GRAY);
        }
    }

    void drawHUD(float dt)
    {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;

        // ── FPS (правый верхний угол, с тёмной подложкой) ──
        if (showFPS) {
            static float ft = 0, fs = 0; static int fn = 0;
            ft += dt; fs += (dt > 0 ? 1.f / dt : 0); fn++;
            if (ft >= 0.25f) { fpsValue = fs / fn; ft = 0; fs = 0; fn = 0; }
            glm::vec4 fc = fpsValue >= 60 ? CON_GREEN : (fpsValue >= 30 ? CON_YELLOW : CON_RED);
            std::ostringstream o; o << std::fixed << std::setprecision(0) << "FPS: " << fpsValue;
            std::string fps = o.str();
            float fw = textRenderer.textWidth(fps);
            textRenderer.drawRect(W - fw - 18.f, 6.f, fw + 12.f, 20.f, glm::vec4(0, 0, 0, 0.45f));
            textRenderer.drawText(fps, W - fw - 12.f, 22.f, fc);
        }

        // ── Позиция (левый верхний, с подложкой) ──
        if (showPos) {
            std::ostringstream o;
            o << std::fixed << std::setprecision(1)
                << "X:" << player.pos.x << "  Y:" << player.pos.y << "  Z:" << player.pos.z;
            std::string ps = o.str();
            float pw = textRenderer.textWidth(ps);
            textRenderer.drawRect(4.f, 6.f, pw + 12.f, 20.f, glm::vec4(0, 0, 0, 0.45f));
            textRenderer.drawText(ps, 10.f, 22.f, CON_CYAN);
        }

        // ── Статус-теги (NOCLIP / GOD) ──
        float sy = 34.f;
        if (noclip) {
            float tw = textRenderer.textWidth("  NOCLIP  ");
            textRenderer.drawRect(4.f, sy - 1.f, tw, 18.f, glm::vec4(0.8f, 0.6f, 0.f, 0.3f));
            textRenderer.drawText("  NOCLIP  ", 4.f, sy + 14.f, CON_YELLOW);
            sy += 22.f;
        }
        if (godMode) {
            float tw = textRenderer.textWidth("  GOD MODE  ");
            textRenderer.drawRect(4.f, sy - 1.f, tw, 18.f, glm::vec4(0.f, 0.7f, 0.f, 0.25f));
            textRenderer.drawText("  GOD MODE  ", 4.f, sy + 14.f, CON_GREEN);
        }

        if (!showHUD) return;

        float cx = W / 2.f, cy = H / 2.f;

        // ── Прицел (крест с точкой) ──
        float cgap = 6.f, clen = 10.f, cw = 1.5f;
        glm::vec4 cc(1, 1, 1, 0.92f);
        // горизонталь
        textRenderer.drawRect(cx - cgap - clen, cy - cw / 2.f, clen, cw, cc);
        textRenderer.drawRect(cx + cgap, cy - cw / 2.f, clen, cw, cc);
        // вертикаль
        textRenderer.drawRect(cx - cw / 2.f, cy - cgap - clen, cw, clen, cc);
        textRenderer.drawRect(cx - cw / 2.f, cy + cgap, cw, clen, cc);
        // центральная точка
        textRenderer.drawRect(cx - 2.f, cy - 2.f, 4.f, 4.f, glm::vec4(1, 1, 1, 0.7f));

        // ── Нижняя панель оружия ──
        const auto& def = weaponManager.activeDef();
        float panelW = 220.f, panelH = 52.f;
        float panelX = W - panelW - 14.f, panelY = H - panelH - 14.f;

        // Фоновая панель
        textRenderer.drawRect(panelX - 4.f, panelY - 4.f, panelW + 8.f, panelH + 8.f,
            glm::vec4(0.f, 0.f, 0.f, 0.55f));
        // Верхняя линия акцента
        textRenderer.drawRect(panelX - 4.f, panelY - 4.f, panelW + 8.f, 2.f,
            glm::vec4(0.9f, 0.7f, 0.2f, 0.8f));

        // Название оружия
        std::string wname = def.file.substr(def.file.rfind('/') + 1);
        wname = wname.substr(0, wname.rfind('.'));
        // Привести к верхнему регистру
        for (auto& c : wname) c = (char)toupper((unsigned char)c);
        textRenderer.drawText(wname, panelX, panelY + 14.f, glm::vec4(0.9f, 0.7f, 0.2f, 1.f));

        // Патроны: большое число / максимум
        int am = gun.ammo, maxAm = def.maxAmmo;
        glm::vec4 amCol = am == 0 ? CON_RED : (am <= 3 ? CON_YELLOW : glm::vec4(1, 1, 1, 1));
        std::string amMain = std::to_string(am);
        std::string amMax = " / " + std::to_string(maxAm);
        float amMainW = textRenderer.textWidth(amMain);
        textRenderer.drawText(amMain, panelX, panelY + 38.f, amCol);
        textRenderer.drawText(amMax, panelX + amMainW, panelY + 38.f, glm::vec4(0.55f, 0.55f, 0.55f, 1));

        // Полоска патронов
        float barX = panelX, barY = panelY + panelH - 4.f, barW = panelW, barH = 3.f;
        textRenderer.drawRect(barX, barY, barW, barH, glm::vec4(0.25f, 0.25f, 0.25f, 0.8f));
        float ratio = (maxAm > 0) ? (float)am / maxAm : 0.f;
        glm::vec4 barCol = am == 0 ? glm::vec4(0.7f, 0.1f, 0.1f, 0.9f) : (am <= 3 ? glm::vec4(0.9f, 0.6f, 0.1f, 0.9f) : glm::vec4(0.3f, 0.8f, 0.4f, 0.9f));
        textRenderer.drawRect(barX, barY, barW * ratio, barH, barCol);

        // ── RELOADING индикатор ──
        if (gun.reloading) {
            std::string rtxt = "[ RELOADING ]";
            float rtw = textRenderer.textWidth(rtxt);
            textRenderer.drawRect(cx - rtw / 2.f - 8.f, cy + 24.f, rtw + 16.f, 20.f,
                glm::vec4(0.f, 0.f, 0.f, 0.6f));
            textRenderer.drawRect(cx - rtw / 2.f - 8.f, cy + 24.f, rtw + 16.f, 2.f,
                glm::vec4(0.9f, 0.7f, 0.2f, 0.9f));
            textRenderer.drawText(rtxt, cx - rtw / 2.f, cy + 40.f, CON_YELLOW);
        }

        // ── NO AMMO индикатор ──
        if (am == 0 && !gun.reloading) {
            std::string ntxt = "[ NO AMMO — R to reload ]";
            float ntw = textRenderer.textWidth(ntxt);
            textRenderer.drawRect(cx - ntw / 2.f - 8.f, cy + 24.f, ntw + 16.f, 20.f,
                glm::vec4(0.f, 0.f, 0.f, 0.6f));
            textRenderer.drawRect(cx - ntw / 2.f - 8.f, cy + 24.f, ntw + 16.f, 2.f,
                glm::vec4(0.8f, 0.1f, 0.1f, 0.9f));
            textRenderer.drawText(ntxt, cx - ntw / 2.f, cy + 40.f, CON_RED);
        }

        // ── HP бар (левый нижний угол) ──
        {
            float hpBarW = 200.f, hpBarH = 10.f;
            float hpX = 14.f, hpY = H - 50.f;
            float ratio = (playerMaxHP > 0) ? playerHP / playerMaxHP : 0.f;
            // Подложка панели
            textRenderer.drawRect(hpX - 4.f, hpY - 20.f, hpBarW + 8.f, 38.f, glm::vec4(0, 0, 0, 0.55f));
            textRenderer.drawRect(hpX - 4.f, hpY - 20.f, hpBarW + 8.f, 2.f, glm::vec4(0.8f, 0.2f, 0.2f, 0.8f));
            // HP label
            std::string hpLabel = "HP";
            textRenderer.drawText(hpLabel, hpX, hpY - 5.f, glm::vec4(0.9f, 0.3f, 0.3f, 1.f));
            float hlw = textRenderer.textWidth(hpLabel) + 6.f;
            // HP число
            std::string hpNum = std::to_string((int)playerHP);
            textRenderer.drawText(hpNum, hpX + hlw, hpY - 5.f, glm::vec4(1, 1, 1, 1));
            // Полоска
            textRenderer.drawRect(hpX, hpY + 8.f, hpBarW, hpBarH, glm::vec4(0.2f, 0.1f, 0.1f, 0.9f));
            glm::vec4 hcol = ratio > 0.5f ? glm::vec4(0.2f, 0.8f, 0.3f, 0.9f)
                : ratio > 0.25f ? glm::vec4(0.9f, 0.6f, 0.1f, 0.9f)
                : glm::vec4(0.85f, 0.1f, 0.1f, 0.9f);
            textRenderer.drawRect(hpX, hpY + 8.f, hpBarW * ratio, hpBarH, hcol);
        }
    }
};

inline Console console;