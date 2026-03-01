#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include "ModelLoader.h"
#include "AnimatedModel.h"

// ──────────────────────────────────────────────
//  Описание одного оружия
// ──────────────────────────────────────────────
struct WeaponDef
{
    std::string file;
    std::string texDir;
    float       scale = 0.01f;
    int         maxAmmo = 12;
    float       fireRate = 0.15f;
    float       recoilKick = 0.04f;
    std::string animIdle;
    std::string animFire;
    std::string animFire001;
    std::string animFire002;
    std::string animReloadEasy;
    std::string animReloadFull;
    std::string animWalk;
    float       rotY = 180.f;
    float       rotX = 0.f;
    float       offsetRight = 0.0f;
    float       offsetUp = -0.25f;
    float       offsetFwd = 0.0f;
    int         slot = 0;
};

// ──────────────────────────────────────────────
//  Список всех оружий
//  Клавиша 1 — листает слот 0 (Glock, P9, Sawnoff)
//  Клавиша 2 — листает слот 1 (AK-74)
// ──────────────────────────────────────────────
inline std::vector<WeaponDef> weaponDefs = {

    // ── СЛОТ 0 (клавиша 1) ───────────────────

    // Glock
    {
        "models/pistol/glock/glock.fbx",
        "models/pistol/glock/textures",
        0.01f, 12, 0.15f, 0.04f,
        "Armature|FPS_Pistol_Idle",
        "Armature|FPS_Pistol_Fire",
        "Armature|FPS_Pistol_Fire.001",
        "Armature|FPS_Pistol_Fire.002",
        "Armature|FPS_Pistol_Reload_easy",
        "Armature|FPS_Pistol_Reload_full",
        "Armature|FPS_Pistol_Walk",
        180.f, 0.f,
        0.0f, -0.25f, 0.0f,
        0
    },

    // P9
    {
        "models/pistol/p9/p9.glb",
        "models/pistol/p9/textures",
        0.01f, 15, 0.13f, 0.04f,
        "WEP_Idle",
        "WEP_Fire",
        "WEP_Fire.001",
        "WEP_Fire.001",
        "WEP_Reload_01",
        "WEP_Reload_01",
        "WEP_Walk",
        180.f, 0.f,
        0.0f, -0.25f, 0.0f,
        0
    },

    // Sawnoff
    {
        "models/pistol/sawnoff/sawnoff.fbx",
        "models/pistol/sawnoff/textures",
        0.01f, 2, 0.8f, 0.08f,
        "WEP_Idle",
        "WEP_Fire",
        "WEP_Fire.001",
        "WEP_Fire.001",
        "WEP_Reload_01",
        "WEP_Reload_01.001",
        "WEP_Walk",
        180.f, 0.f,
        0.0f, -0.25f, 0.08f,
        0
    },

    // ── СЛОТ 1 (клавиша 2) ───────────────────

    // AK-74
    {
        "models/pistol/ak74/ak74.fbx",
        "models/pistol/ak74/textures",
        0.01f, 30, 0.1f, 0.06f,
        "Rig|AK_Idle",
        "Rig|AK_Shot",
        "Rig|AK_Shot",
        "Rig|AK_Shot",
        "Rig|AK_Reload",
        "Rig|AK_Reload_full",
        "Rig|AK_Run",
        180.f, 0.f,
        0.0f, -0.25f, 0.0f,
        1
    },
};

// ──────────────────────────────────────────────
//  Forward declarations из Player.h
// ──────────────────────────────────────────────
struct GunState;
extern GunState gun;
extern float flashTimer;
extern int   fireAnimCounter;

// ──────────────────────────────────────────────
//  WeaponManager
// ──────────────────────────────────────────────
struct WeaponManager
{
    std::vector<std::unique_ptr<AnimatedModel>> models;
    int current = 0;

    void loadAll()
    {
        models.resize(weaponDefs.size());
        for (int i = 0; i < (int)weaponDefs.size(); i++)
        {
            models[i] = std::make_unique<AnimatedModel>();
            auto& def = weaponDefs[i];
            models[i]->texLoader = loadTexture;
            models[i]->load(def.file, def.texDir);
            if (!def.animIdle.empty() && models[i]->hasAnim(def.animIdle))
                models[i]->play(def.animIdle, true);
            std::cout << "[WEAPON] Loaded #" << i << ": " << def.file << "\n";
        }
    }

    void switchTo(int idx)
    {
        if (idx < 0 || idx >= (int)models.size()) return;
        if (idx == current) return;
        current = idx;
        auto& def = weaponDefs[current];
        auto& gm = *models[current];

        gun.ammo = def.maxAmmo;
        gun.reloading = false;
        gun.shootCooldown = 0.f;
        gun.recoilOffset = 0.f;
        gun.recoilTimer = 0.f;
        flashTimer = 0.f;

        gm.curAnim = "";
        if (!def.animIdle.empty() && gm.hasAnim(def.animIdle))
            gm.play(def.animIdle, true);

        std::cout << "[WEAPON] Switched to #" << current
            << " slot:" << def.slot << " " << def.file << "\n";
    }

    // Нажата клавиша слота — переключить внутри слота
    void pressSlot(int slot)
    {
        std::vector<int> inSlot;
        for (int i = 0; i < (int)weaponDefs.size(); i++)
            if (weaponDefs[i].slot == slot)
                inSlot.push_back(i);

        if (inSlot.empty()) return;

        // Если уже в этом слоте — берём следующее, иначе первое
        int next = inSlot[0];
        for (int i = 0; i < (int)inSlot.size(); i++) {
            if (inSlot[i] == current) {
                next = inSlot[(i + 1) % inSlot.size()];
                break;
            }
        }
        switchTo(next);
    }

    AnimatedModel& active() { return *models[current]; }
    const WeaponDef& activeDef() const { return weaponDefs[current]; }
};

inline WeaponManager weaponManager;