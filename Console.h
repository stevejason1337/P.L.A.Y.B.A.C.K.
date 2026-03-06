#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include "TextRenderer.h"
#include "Settings.h"

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

// forward declarations
struct EnemyManager;
extern EnemyManager enemyManager;
struct Character;
extern Character player;
struct GunState;
extern GunState gun;
extern float playerHP;
extern float playerMaxHP;

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
                // forward — определяется в WeaponManager.h
                extern struct WeaponManager weaponManager;
                gun.ammo = weaponManager.activeDef().maxAmmo;
                print("Ammo refilled.", CON_GREEN);
            }
            else if (what == "weapon") {
                int idx = 0; ss >> idx;
                extern struct WeaponManager weaponManager;
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
            print("Spawned " + std::to_string(n) + " soldier(s).", CON_GREEN);
        }
        else if (name == "zombie") {
            int n = 1; ss >> n; if (n < 1)n = 1; if (n > 20)n = 20;
            glm::vec3 spawnPos = player.pos + glm::vec3(5, 0, 0);
            enemyManager.spawnZombieGroup(spawnPos, n, n > 1 ? 4.f : 0.f);
            print("Spawned " + std::to_string(n) + " zombie(s).", CON_GREEN);
        }
        else if (name == "zscale") {
            float s = 0.01f; ss >> s;
            for (auto& e : enemyManager.enemies)
                if (e.type == EnemyType::ZOMBIE) e.scale = s;
            print("Zombie scale: " + std::to_string(s), CON_GREEN);
        }
        else if (name == "zspeed") {
            float s = 1.0f; ss >> s;
            for (auto& e : enemyManager.enemies)
                if (e.type == EnemyType::ZOMBIE) e.animSpeed = s;
            print("Zombie animSpeed: " + std::to_string(s), CON_GREEN);
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
            "spawn","zombie","killall","enemies","clear","quit"
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

};

inline Console console;