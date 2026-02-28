#pragma once

#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <limits>

#include "Settings.h"
#include "AABB.h"

// ──────────────────────────────────────────────
//  Player
// ──────────────────────────────────────────────
struct Character {
    glm::vec3 pos = glm::vec3(0.f, 200.f, 0.f);
    glm::vec3 vel = glm::vec3(0.f);
    float     eyeH = STAND_H;
    bool      onGround = false;
    bool      crouching = false;
    bool      sprinting = false;
};
inline Character player;

// ──────────────────────────────────────────────
//  Gun
// ──────────────────────────────────────────────
struct GunState {
    int   ammo = 12;
    float shootCooldown = 0.f;
    float recoilOffset = 0.f;
    float recoilTimer = 0.f;
    float bobTimer = 0.f;
    bool  reloading = false;
    bool  reloadFull = false;
};
inline GunState gun;
inline float    flashTimer = 0.f;
inline int      fireAnimCounter = 0;

struct BulletHole { glm::vec3 pos; float life; };
inline std::vector<BulletHole> bulletHoles;

// ──────────────────────────────────────────────
//  Auto spawn
// ──────────────────────────────────────────────
inline void autoSpawn()
{
    if (bvh.nodes.empty()) return;
    glm::vec3 center = (bvh.worldMin + bvh.worldMax) * 0.5f;
    float searchY = bvh.worldMax.y + 10.f;
    glm::vec3 o = glm::vec3(center.x, searchY, center.z);
    float hitDist;
    if (bvh.raycast(o, { 0,-1,0 }, searchY - bvh.worldMin.y + 10.f, hitDist)) {
        player.pos = glm::vec3(center.x, o.y - hitDist + 0.1f, center.z);
        std::cout << "[SPAWN] (" << player.pos.x << "," << player.pos.y << "," << player.pos.z << ")\n";
    }
    else {
        player.pos = glm::vec3(center.x, bvh.worldMax.y + 5.f, center.z);
        std::cout << "[SPAWN] Fallback\n";
    }
}

// ──────────────────────────────────────────────
//  Physics
// ──────────────────────────────────────────────
inline void updatePlayer(float dt)
{
    if (!player.onGround) player.vel.y += GRAVITY * dt;
    player.pos += player.vel * dt;

    float gy = getGroundY(player.pos);
    if (gy != std::numeric_limits<float>::lowest()) {
        if (player.pos.y <= gy + 0.05f) {
            player.pos.y = gy; player.vel.y = 0.f; player.onGround = true;
        }
        else { player.onGround = false; }
    }
    else {
        if (player.pos.y <= 0.f) {
            player.pos.y = 0.f; player.vel.y = 0.f; player.onGround = true;
        }
        else { player.onGround = false; }
    }

    wallCollide(player.pos);

    float killY = bvh.nodes.empty() ? -300.f : bvh.worldMin.y - 50.f;
    if (player.pos.y < killY) {
        autoSpawn(); player.vel = glm::vec3(0);
        std::cout << "[RESET] Respawned\n";
    }

    float th = player.crouching ? CROUCH_H : STAND_H;
    player.eyeH += (th - player.eyeH) * 12.f * dt;
}

// ──────────────────────────────────────────────
//  Gun timers
// ──────────────────────────────────────────────
inline void updateGun(float dt)
{
    if (gun.recoilTimer > 0) {
        gun.recoilTimer -= dt;
        gun.recoilOffset = RECOIL_KICK * (gun.recoilTimer / 0.1f);
    }
    else { gun.recoilOffset *= 0.85f; }

    if (gun.shootCooldown > 0) gun.shootCooldown -= dt;
    if (flashTimer > 0)        flashTimer -= dt;

    for (int i = (int)bulletHoles.size() - 1; i >= 0; i--) {
        bulletHoles[i].life -= dt;
        if (bulletHoles[i].life <= 0)
            bulletHoles.erase(bulletHoles.begin() + i);
    }
}

// ──────────────────────────────────────────────
//  Shoot — принимает параметры текущего оружия
// ──────────────────────────────────────────────
inline void doShoot(const glm::vec3& camPos, const glm::vec3& camFront,
    float fireRate, float recoilKick)
{
    if (gun.shootCooldown > 0 || gun.ammo <= 0 || gun.reloading) return;
    gun.ammo--;
    gun.shootCooldown = fireRate;
    gun.recoilTimer = 0.1f;
    flashTimer = 0.06f;
    fireAnimCounter++;
    glm::vec3 hitPos;
    if (shootRay(camPos, camFront, hitPos)) bulletHoles.push_back({ hitPos, 5.f });
    std::cout << "[SHOOT] Ammo:" << gun.ammo << "\n";
    if (gun.ammo <= 0) std::cout << "[SHOOT] Empty! R to reload.\n";
}

// ──────────────────────────────────────────────
//  Reload — maxAmmo передаётся снаружи
// ──────────────────────────────────────────────
inline void doReload(int maxAmmo)
{
    if (gun.reloading || gun.ammo >= maxAmmo) return;
    gun.reloading = true;
    gun.reloadFull = (gun.ammo == 0);
    std::cout << "[RELOAD] " << (gun.reloadFull ? "Full" : "Tactical") << "\n";
}