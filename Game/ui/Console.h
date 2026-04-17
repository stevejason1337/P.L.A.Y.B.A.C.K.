#pragma once
// ════════════════════════════════════════════════════════════
//  Console.h  -  Developer console через Dear ImGui
//  Работает одинаково на OpenGL и DX11
// ════════════════════════════════════════════════════════════
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include "Settings.h"
#include <imgui.h>
#include <GLFW/glfw3.h>

// --- игровые флаги -------------------------------------------
inline bool  noclip = false;
inline bool  gShouldQuit = false;   // set by "quit" command, checked in main loop
inline bool  godMode = false;
inline bool  showFPS = true;
inline bool  showPos = true;
inline bool  showHUD = true;
inline float timeScale = 1.f;
inline bool  wireframe = false;
inline float fpsValue = 0.f;

// --- forward declarations ------------------------------------
struct EnemyManager; extern EnemyManager enemyManager;
struct Character;    extern Character    player;
extern glm::vec3 camFront;
struct GunState;     extern GunState     gun;
extern float playerHP, playerMaxHP;

// --- колбэки (устанавливаются из main.cpp) -------------------
inline void(*gSetWireframe)(bool) = nullptr;
inline const char* (*gGetBackendName)() = nullptr;

// --- цвета ---------------------------------------------------
#define CON_WHITE  ImVec4(0.85f,0.85f,0.85f,1.f)
#define CON_YELLOW ImVec4(1.f,0.80f,0.20f,1.f)
#define CON_RED    ImVec4(1.f,0.30f,0.30f,1.f)
#define CON_GREEN  ImVec4(0.40f,1.f,0.40f,1.f)
#define CON_CYAN   ImVec4(0.40f,0.90f,1.f,1.f)
#define CON_GRAY   ImVec4(0.55f,0.55f,0.55f,1.f)

struct ConLine {
    std::string text;
    ImVec4      color;
};

// ════════════════════════════════════════════════════════════
struct Console
{
    bool open = false;
    bool scrollBot = false;

    std::string              input;
    std::vector<ConLine>     lines;
    std::vector<std::string> history;
    int                      histIdx = -1;

    void init()
    {
        print("=== 3D Engine Developer Console ===", CON_YELLOW);
        print("Type 'help' for commands.", CON_GRAY);
        print("", CON_WHITE);
    }

    void print(const std::string& text, ImVec4 col = CON_WHITE)
    {
        lines.push_back({ text, col });
        if ((int)lines.size() > 512) lines.erase(lines.begin());
        scrollBot = true;
    }

    void toggle()
    {
        open = !open;
        if (open) { input = ""; histIdx = -1; scrollBot = true; }
    }

    void update(float) {}

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
            print("  noclip              - fly through walls", CON_CYAN);
            print("  god                 - god mode", CON_CYAN);
            print("  kill                - respawn", CON_CYAN);
            print("  give ammo           - refill ammo", CON_CYAN);
            print("  give weapon <N>     - switch weapon", CON_CYAN);
            print("  setpos <x> <y> <z>  - teleport", CON_CYAN);
            print("  timescale <f>       - time multiplier", CON_CYAN);
            print("--- Display ---", CON_YELLOW);
            print("  fps / pos / hud     - toggle overlays", CON_CYAN);
            print("  wireframe           - toggle wireframe", CON_CYAN);
            print("--- Enemies ---", CON_YELLOW);
            print("  spawn [N]           - spawn N soldiers", CON_CYAN);
            print("  zombie [N]          - spawn N zombies", CON_CYAN);
            print("  pig_demon [N]       - spawn N pig demons", CON_CYAN);
            print("  killall             - kill all enemies", CON_CYAN);
            print("  enemies             - show enemy count", CON_CYAN);
            print("--- Graphics ---", CON_YELLOW);
            print("  gfx_api             - show current API", CON_CYAN);
            print("  gfx_switch          - switch API (restart)", CON_CYAN);
            print("--- Other ---", CON_YELLOW);
            print("  clear / quit        - clear / exit", CON_CYAN);
        }
        else if (name == "noclip") {
            noclip = !noclip;
            print(noclip ? "Noclip ON" : "Noclip OFF", CON_GREEN);
        }
        else if (name == "god") {
            godMode = !godMode;
            print(godMode ? "God mode ON" : "God mode OFF", CON_GREEN);
        }
        else if (name == "kill") {
            player.pos.y = -9999.f; playerHP = playerMaxHP;
            print("Killed. Respawning...", CON_RED);
        }
        else if (name == "give") {
            std::string what; ss >> what;
            std::transform(what.begin(), what.end(), what.begin(), ::tolower);
            if (what == "ammo") {
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
                player.pos = glm::vec3(x, y, z); player.vel = glm::vec3(0);
                std::ostringstream o; o << std::fixed << std::setprecision(1);
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
            if (gSetWireframe) gSetWireframe(wireframe);
            print(wireframe ? "Wireframe ON" : "Wireframe OFF", CON_GREEN);
        }
        else if (name == "gfx_api") {
            std::string api = gGetBackendName ? gGetBackendName() : "unknown";
            print("Current API: " + api, CON_CYAN);
        }
        else if (name == "gfx_switch") {
#ifdef _WIN32
            gRenderBackend = (gRenderBackend == RenderBackend::OpenGL)
                ? RenderBackend::DX11 : RenderBackend::OpenGL;
            saveEngineConfig();
            std::string api = (gRenderBackend == RenderBackend::DX11) ? "DX11" : "OpenGL";
            print("Switching to " + api + " on restart. Saved to engine.cfg.", CON_YELLOW);
#else
            print("DX11 not available on this platform.", CON_RED);
#endif
        }
        else if (name == "spawn") {
            int n = 1; ss >> n; n = glm::clamp(n, 1, 20);
            glm::vec3 fwd = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
            enemyManager.spawnGroup(player.pos + fwd * 5.f, n, n > 1 ? 4.f : 0.f);
            print("Spawned " + std::to_string(n) + " soldier(s).", CON_GREEN);
        }
        else if (name == "zombie") {
            int n = 1; ss >> n; n = glm::clamp(n, 1, 20);
            glm::vec3 fwd = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
            enemyManager.spawnZombieGroup(player.pos + fwd * 5.f, n, n > 1 ? 4.f : 0.f);
            print("Spawned " + std::to_string(n) + " zombie(s).", CON_GREEN);
        }
        else if (name == "zombie2") {
            int n = 1; ss >> n; n = glm::clamp(n, 1, 20);
            glm::vec3 fwd = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
            enemyManager.spawnZombie2Group(player.pos + fwd * 5.f, n, n > 1 ? 4.f : 0.f);
            print("Spawned " + std::to_string(n) + " zombie2(s).", CON_GREEN);
        }
        else if (name == "pig_demon" || name == "pig" || name == "demon") {
            int n = 1; ss >> n; n = glm::clamp(n, 1, 10);
            glm::vec3 fwd = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
            glm::vec3 spawnPos = player.pos + fwd * 6.f;
            if (n == 1) {
                enemyManager.spawnPigDemon(spawnPos);
            }
            else {
                enemyManager.spawnPigDemonGroup(spawnPos, n, n > 3 ? 5.f : 3.f);
            }
            print("Spawned " + std::to_string(n) + " pig demon(s). Watch out!", CON_RED);
        }
        else if (name == "nocull") {
            enemyManager.debugNoCull = !enemyManager.debugNoCull;
            print(enemyManager.debugNoCull ? "Culling OFF" : "Culling ON", CON_GREEN);
        }
        else if (name == "zscale") {
            float s = 1.f; ss >> s;
            for (auto& e : enemyManager.enemies)
                if (e.type == EnemyType::ZOMBIE) e.scale = s;
            print("Zombie scale: " + std::to_string(s), CON_GREEN);
        }
        else if (name == "zspeed") {
            float s = 1.f; ss >> s;
            for (auto& e : enemyManager.enemies)
                if (e.type == EnemyType::ZOMBIE) e.animSpeed = s;
            print("Zombie animSpeed: " + std::to_string(s), CON_GREEN);
        }
        else if (name == "killall") {
            int cnt = 0;
            for (auto& e : enemyManager.enemies)
                if (!e.isDead()) { e.takeDamage(9999.f); cnt++; }
            print("Killed " + std::to_string(cnt) + " enemies.", CON_GREEN);
        }
        else if (name == "enemies") {
            print("Enemies alive: " + std::to_string(enemyManager.enemies.size()), CON_CYAN);
        }
        else if (name == "clear") { lines.clear(); }
        else if (name == "quit" || name == "exit") { print("Goodbye.", CON_YELLOW); gShouldQuit = true; }
        else if (name == "disconnect") {
            print("Returning to main menu...", CON_YELLOW);
            extern bool gReturnToMenu;
            gReturnToMenu = true;
        }
        else { print("Unknown command: '" + name + "'  (type 'help')", CON_RED); }

        input.clear();
    }

    // Called from key_callback in Input.h
    void keyInput(int key, int action)
    {
        if (!open) return;
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_ENTER) {
                // НЕ вызываем execute() здесь — ImGui InputText с
                // EnterReturnsTrue уже обрабатывает Enter и вызывает execute()
                // Двойной вызов = двойной спавн и т.д.
            }
            else if (key == GLFW_KEY_BACKSPACE && !input.empty()) {
                input.pop_back();
            }
            else if (key == GLFW_KEY_UP) {
                if (!history.empty()) {
                    histIdx = std::min((int)history.size() - 1, histIdx + 1);
                    input = history[(int)history.size() - 1 - histIdx];
                }
            }
            else if (key == GLFW_KEY_DOWN) {
                if (histIdx > 0) {
                    histIdx--;
                    input = history[(int)history.size() - 1 - histIdx];
                }
                else { histIdx = -1; input.clear(); }
            }
            else if (key == GLFW_KEY_TAB) {
                autoComplete();
            }
            else if (key == GLFW_KEY_ESCAPE) {
                open = false;
            }
        }
    }

    // Called from char_callback in Input.h
    void charInput(unsigned int c)
    {
        if (!open) return;
        if (c >= 32 && c < 127)
            input += (char)c;
    }

    void autoComplete()
    {
        static const std::vector<std::string> cmds = {
            "help","noclip","god","kill","give ammo","give weapon",
            "setpos","timescale","fps","pos","hud","wireframe",
            "spawn","zombie","zombie2","pig_demon","killall","enemies","nocull",
            "zscale","zspeed","clear","quit","gfx_api","gfx_switch"
        };
        std::vector<std::string> matches;
        for (auto& c : cmds)
            if (c.size() >= input.size() && c.substr(0, input.size()) == input)
                matches.push_back(c);
        if (matches.size() == 1) { input = matches[0]; }
        else if (matches.size() > 1) {
            print("", CON_WHITE);
            for (auto& m : matches) print("  " + m, CON_CYAN);
        }
    }

    // ════════════════════════════════════════════════════════
    //  DRAW
    // ════════════════════════════════════════════════════════
    void draw()
    {
        if (!open) return;

        ImGuiIO& io = ImGui::GetIO();
        float W = io.DisplaySize.x;
        float H = io.DisplaySize.y;

        ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ W, H * 0.50f }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.07f, 0.05f, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.45f, 0.25f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.12f, 0.08f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.10f, 0.16f, 0.10f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));

        if (ImGui::Begin("##console", nullptr, flags))
        {
            // Заголовок
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.2f, 1.f));
            ImGui::Text("DEVELOPER CONSOLE");
            ImGui::PopStyleColor();
            ImGui::SameLine(W * 0.5f - 80.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 0.8f));
            std::string ver = "3D Engine  [";
            ver += (gGetBackendName ? gGetBackendName() : "?");
            ver += "]";
            ImGui::Text("%s", ver.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            // Лог
            float footH = ImGui::GetFrameHeightWithSpacing() + 8.f;
            ImGui::BeginChild("##log", { 0, -footH }, false,
                ImGuiWindowFlags_HorizontalScrollbar);
            for (auto& l : lines) {
                ImGui::PushStyleColor(ImGuiCol_Text, l.color);
                ImGui::TextUnformatted(l.text.c_str());
                ImGui::PopStyleColor();
            }
            if (scrollBot) { ImGui::SetScrollHereY(1.f); scrollBot = false; }
            ImGui::EndChild();

            ImGui::Separator();

            // Ввод
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.95f, 0.9f, 1.f));
            ImGui::Text("]");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushItemWidth(-1.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.12f, 0.08f, 1.f));

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            auto inputCB = [](ImGuiInputTextCallbackData* d) -> int {
                Console* con = (Console*)d->UserData;
                if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                    if (d->EventKey == ImGuiKey_UpArrow && !con->history.empty()) {
                        con->histIdx = std::min((int)con->history.size() - 1, con->histIdx + 1);
                        std::string h = con->history[(int)con->history.size() - 1 - con->histIdx];
                        d->DeleteChars(0, d->BufTextLen);
                        d->InsertChars(0, h.c_str());
                    }
                    else if (d->EventKey == ImGuiKey_DownArrow) {
                        if (con->histIdx > 0) {
                            con->histIdx--;
                            std::string h = con->history[(int)con->history.size() - 1 - con->histIdx];
                            d->DeleteChars(0, d->BufTextLen);
                            d->InsertChars(0, h.c_str());
                        }
                        else { con->histIdx = -1; d->DeleteChars(0, d->BufTextLen); }
                    }
                }
                if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
                    con->input = std::string(d->Buf, d->BufTextLen);
                    con->autoComplete();
                    d->DeleteChars(0, d->BufTextLen);
                    d->InsertChars(0, con->input.c_str());
                }
                return 0;
                };

            char buf[512];
#pragma warning(suppress: 4996)
            strncpy(buf, input.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';

            if (ImGui::InputText("##inp", buf, sizeof(buf),
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_CallbackHistory |
                ImGuiInputTextFlags_CallbackCompletion,
                inputCB, this))
            {
                input = buf;
                execute(input);
                ImGui::SetKeyboardFocusHere(-1);
            }
            else {
                input = buf;
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }
};

inline Console console;