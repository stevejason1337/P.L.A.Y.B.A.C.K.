#pragma once

// Window
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// Camera
const float FOV = 75.f;
const float PITCH_LIM = 75.f;
const float MOUSE_SENS = 0.1f;

// Map
const float MAP_SCALE = 0.15f;
const float MAP_ROT_X = 0.0f;
const char* MAP_FILE = "models/maps/forest/forest.fbx";
const char* MAP_TEX_DIR = "models/maps/forest/textures";

// Player
const float GRAVITY = -25.f;
const float JUMP_FORCE = 8.f;
const float WALK_SPEED = 5.f;
const float SPRINT_SPEED = 10.f;
const float CROUCH_SPEED = 2.5f;
const float STAND_H = 1.7f;
const float CROUCH_H = 0.9f;

// ──────────────────────────────────────────────
//  Универсальное положение рук (все оружия)
//  Меняй эти значения — двигаются ВСЕ оружия сразу
// ──────────────────────────────────────────────
const float GUN_OFFSET_RIGHT = 0.0f;   // влево/вправо
const float GUN_OFFSET_UP = -0.25f; // вверх/вниз
const float GUN_OFFSET_FWD = 0.0f;   // вперёд/назад

// Recoil
const float RECOIL_KICK = 0.04f;

// Дефолтные значения (для совместимости)
const float GUN_SCALE = 0.01f;
const char* GUN_FILE = "models/pistol/glock/glock.fbx";
const char* GUN_TEX_DIR = "models/pistol/glock/textures";
const float FIRE_RATE = 0.15f;

// Анимации пистолета
const char* ANIM_IDLE = "Armature|FPS_Pistol_Idle";
const char* ANIM_FIRE = "Armature|FPS_Pistol_Fire";
const char* ANIM_FIRE_001 = "Armature|FPS_Pistol_Fire.001";
const char* ANIM_FIRE_002 = "Armature|FPS_Pistol_Fire.002";
const char* ANIM_RELOAD_EASY = "Armature|FPS_Pistol_Reload_easy";
const char* ANIM_RELOAD_FULL = "Armature|FPS_Pistol_Reload_full";
const char* ANIM_WALK = "Armature|FPS_Pistol_Walk";