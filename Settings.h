#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <string>
#include <fstream>
#include <iostream>

// ── Window ────────────────────────────────────────────────────
inline unsigned int SCR_WIDTH = 1280;
inline unsigned int SCR_HEIGHT = 720;

// ── Camera ────────────────────────────────────────────────────
inline constexpr float FOV = 75.f;
inline constexpr float GUN_FOV = 68.f;
inline constexpr float PITCH_LIM = 75.f;
inline constexpr float MOUSE_SENS = 0.1f;

// ── Map ───────────────────────────────────────────────────────
inline constexpr float MAP_SCALE = 0.05f;
inline constexpr float MAP_ROT_X = -90.0f;
// const char* без inline = отдельная копия в каждой единице трансляции
// С inline constexpr — одна копия на всю программу (C++17)
inline constexpr const char* MAP_FILE = "models/maps/awp_lego/awp_lego.fbx";
inline constexpr const char* MAP_TEX_DIR = "models/maps/awp_lego/textures";

// ── Player ────────────────────────────────────────────────────
inline constexpr float GRAVITY = -25.f;
inline constexpr float JUMP_FORCE = 8.f;
inline constexpr float WALK_SPEED = 5.f;
inline constexpr float SPRINT_SPEED = 10.f;
inline constexpr float CROUCH_SPEED = 2.5f;
inline constexpr float STAND_H = 1.7f;
inline constexpr float CROUCH_H = 0.9f;

// ── Gun offsets ───────────────────────────────────────────────
inline constexpr float GUN_OFFSET_RIGHT = 0.1f;
inline constexpr float GUN_OFFSET_UP = -0.25f;
inline constexpr float GUN_OFFSET_FWD = 0.0f;

// ── Recoil / defaults ─────────────────────────────────────────
inline constexpr float RECOIL_KICK = 0.04f;
inline constexpr float GUN_SCALE = 0.01f;
inline constexpr const char* GUN_FILE = "models/pistol/glock/glock.fbx";
inline constexpr const char* GUN_TEX_DIR = "models/pistol/glock/textures";
inline constexpr float FIRE_RATE = 0.15f;

// ── Animations ────────────────────────────────────────────────
inline constexpr const char* ANIM_IDLE = "Armature|FPS_Pistol_Idle";
inline constexpr const char* ANIM_FIRE = "Armature|FPS_Pistol_Fire";
inline constexpr const char* ANIM_FIRE_001 = "Armature|FPS_Pistol_Fire.001";
inline constexpr const char* ANIM_FIRE_002 = "Armature|FPS_Pistol_Fire.002";
inline constexpr const char* ANIM_RELOAD_EASY = "Armature|FPS_Pistol_Reload_easy";
inline constexpr const char* ANIM_RELOAD_FULL = "Armature|FPS_Pistol_Reload_full";
inline constexpr const char* ANIM_WALK = "Armature|FPS_Pistol_Walk";

// ── Graphics API ─────────────────────────────────────────────
enum class RenderBackend { OpenGL, DX11 };

inline RenderBackend gRenderBackend = RenderBackend::OpenGL;
inline constexpr const char* ENGINE_CFG = "engine.cfg";

inline void loadEngineConfig()
{
    std::ifstream f(ENGINE_CFG);
    if (!f.is_open()) {
        gRenderBackend = RenderBackend::OpenGL;
        return;
    }
    std::string key, val;
    while (f >> key >> val) {
        if (key == "graphics_api") {
            gRenderBackend = (val == "dx11" || val == "DX11")
                ? RenderBackend::DX11
                : RenderBackend::OpenGL;
        }
    }
    std::cout << "[Config] Graphics API: "
        << (gRenderBackend == RenderBackend::DX11 ? "DirectX 11" : "OpenGL")
        << "\n";
}

inline void saveEngineConfig()
{
    std::ofstream f(ENGINE_CFG);
    if (!f.is_open()) { std::cerr << "[Config] Cannot write engine.cfg\n"; return; }
    f << "graphics_api "
        << (gRenderBackend == RenderBackend::DX11 ? "dx11" : "opengl") << "\n";
    std::cout << "[Config] Saved graphics_api = "
        << (gRenderBackend == RenderBackend::DX11 ? "dx11" : "opengl") << "\n";
}