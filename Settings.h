#pragma once

// Отключаем макросы min/max из Windows.h (конфликт со std::min/std::max)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <string>
#include <fstream>
#include <iostream>

// Window
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// Camera
const float FOV = 75.f;
const float GUN_FOV = 68.f;
const float PITCH_LIM = 75.f;
const float MOUSE_SENS = 0.1f;

// Map
const float MAP_SCALE = 0.05f;
const float MAP_ROT_X = -90.0f;
const char* MAP_FILE = "models/maps/awp_lego/awp_lego.fbx";
const char* MAP_TEX_DIR = "models/maps/awp_lego/textures";

// Player
const float GRAVITY = -25.f;
const float JUMP_FORCE = 8.f;
const float WALK_SPEED = 5.f;
const float SPRINT_SPEED = 10.f;
const float CROUCH_SPEED = 2.5f;
const float STAND_H = 1.7f;
const float CROUCH_H = 0.9f;

// Gun offsets
const float GUN_OFFSET_RIGHT = 0.1f;
const float GUN_OFFSET_UP = -0.25f;
const float GUN_OFFSET_FWD = 0.0f;

// Recoil / defaults
const float RECOIL_KICK = 0.04f;
const float GUN_SCALE = 0.01f;
const char* GUN_FILE = "models/pistol/glock/glock.fbx";
const char* GUN_TEX_DIR = "models/pistol/glock/textures";
const float FIRE_RATE = 0.15f;

// Animations
const char* ANIM_IDLE = "Armature|FPS_Pistol_Idle";
const char* ANIM_FIRE = "Armature|FPS_Pistol_Fire";
const char* ANIM_FIRE_001 = "Armature|FPS_Pistol_Fire.001";
const char* ANIM_FIRE_002 = "Armature|FPS_Pistol_Fire.002";
const char* ANIM_RELOAD_EASY = "Armature|FPS_Pistol_Reload_easy";
const char* ANIM_RELOAD_FULL = "Armature|FPS_Pistol_Reload_full";
const char* ANIM_WALK = "Armature|FPS_Pistol_Walk";

// ─────────────────────────────────────────────────────────────
//  GRAPHICS API SELECTION — сохраняется в engine.cfg
//  Как в Doom: запускаешь → выбираешь → сохраняется → при след.
//  запуске читается автоматически
// ─────────────────────────────────────────────────────────────
enum class RenderBackend { OpenGL, DX11 };

// Глобальный выбор бэкенда — меняется до renderer.init()
inline RenderBackend gRenderBackend = RenderBackend::DX11;

inline const char* ENGINE_CFG = "engine.cfg";

// Читает engine.cfg и устанавливает gRenderBackend
// Вызывай в самом начале main() ДО создания окна
inline void loadEngineConfig()
{
    std::ifstream f(ENGINE_CFG);
    if (!f.is_open()) {
        // No config file — default to DX11
        gRenderBackend = RenderBackend::DX11;
        return;
    }
    std::string key, val;
    while (f >> key >> val) {
        if (key == "graphics_api") {
            if (val == "dx11" || val == "DX11")
                gRenderBackend = RenderBackend::DX11;
            else
                gRenderBackend = RenderBackend::OpenGL;
        }
    }
    std::cout << "[Config] Graphics API: "
        << (gRenderBackend == RenderBackend::DX11 ? "DirectX 11" : "OpenGL")
        << "\n";
}

// Сохраняет текущий выбор в engine.cfg
inline void saveEngineConfig()
{
    std::ofstream f(ENGINE_CFG);
    if (!f.is_open()) { std::cerr << "[Config] Cannot write engine.cfg\n"; return; }
    f << "graphics_api " << (gRenderBackend == RenderBackend::DX11 ? "dx11" : "opengl") << "\n";
    std::cout << "[Config] Saved graphics_api = "
        << (gRenderBackend == RenderBackend::DX11 ? "dx11" : "opengl") << "\n";
}