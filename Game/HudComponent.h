#pragma once

// ============================================================
//  Game/HudComponent.h  —  ИГРА, трогаешь здесь
//  HUD через ImGui. Рисует здоровье, патроны, прицел.
//  Движок вызывает onRenderUI() из GameScene.
// ============================================================

#include "../Engine/Component.h"

// ImGui (в корне проекта)
#include "../imgui.h"

// Указатели на данные (не владеет ими)
struct HudData {
    float* health       = nullptr;
    int*   ammo         = nullptr;
    int*   reserveAmmo  = nullptr;
    std::string* weaponName = nullptr;
    int*   score        = nullptr;
    bool*  playerAlive  = nullptr;
};

class HudComponent : public Component {
public:
    HudData data;
    bool    showDebug = false;

    void renderUI() {
        renderCrosshair();
        renderHealthBar();
        renderAmmoCounter();
        renderScore();

        if (!data.playerAlive || !*data.playerAlive)
            renderDeathScreen();

        if (showDebug)
            renderDebug();
    }

private:
    void renderCrosshair() {
        ImGuiIO& io = ImGui::GetIO();
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float sz = 8.f;
        const float gap = 3.f;
        ImU32 col = IM_COL32(255, 255, 255, 200);
        dl->AddLine({cx - sz - gap, cy}, {cx - gap, cy}, col, 1.5f);
        dl->AddLine({cx + gap, cy}, {cx + sz + gap, cy}, col, 1.5f);
        dl->AddLine({cx, cy - sz - gap}, {cx, cy - gap}, col, 1.5f);
        dl->AddLine({cx, cy + gap}, {cx, cy + sz + gap}, col, 1.5f);
    }

    void renderHealthBar() {
        if (!data.health) return;
        float hp = *data.health;

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({20.f, io.DisplaySize.y - 80.f});
        ImGui::SetNextWindowSize({220.f, 60.f});
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("##hp", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoInputs);

        ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "HP");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        ImVec4 barColor = hp > 50.f ? ImVec4(0.1f,0.9f,0.2f,1.f)
                        : hp > 25.f ? ImVec4(0.9f,0.7f,0.1f,1.f)
                        :             ImVec4(0.9f,0.1f,0.1f,1.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(hp / 100.f, {-1, 0}, "");
        ImGui::PopStyleColor();
        ImGui::Text("%.0f / 100", hp);
        ImGui::End();
    }

    void renderAmmoCounter() {
        if (!data.ammo) return;
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({io.DisplaySize.x - 160.f, io.DisplaySize.y - 80.f});
        ImGui::SetNextWindowSize({150.f, 60.f});
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("##ammo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoInputs);

        if (data.weaponName)
            ImGui::TextColored({0.8f,0.8f,0.8f,1.f}, "%s", data.weaponName->c_str());

        ImGui::TextColored({1.f,1.f,0.3f,1.f}, "%d", *data.ammo);
        ImGui::SameLine();
        ImGui::TextColored({0.6f,0.6f,0.6f,1.f}, "/ %d", data.reserveAmmo ? *data.reserveAmmo : 0);
        ImGui::End();
    }

    void renderScore() {
        if (!data.score) return;
        ImGui::SetNextWindowPos({20.f, 20.f});
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("##score", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs);
        ImGui::TextColored({1.f,0.8f,0.f,1.f}, "SCORE: %d", *data.score);
        ImGui::End();
    }

    void renderDeathScreen() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##death", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);

        float cx = io.DisplaySize.x * 0.5f - 80.f;
        float cy = io.DisplaySize.y * 0.5f - 20.f;
        ImGui::SetCursorPos({cx, cy});
        ImGui::TextColored({1.f,0.1f,0.1f,1.f}, "YOU DIED");
        ImGui::End();
    }

    void renderDebug() {
        ImGui::Begin("Debug");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        auto& cam = Renderer::get().camera;
        ImGui::Text("Cam pos: %.1f %.1f %.1f", cam.position.x, cam.position.y, cam.position.z);
        ImGui::End();
    }
};
