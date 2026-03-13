#pragma once
// Enemy.h включён через main.cpp/Input.h — enemyManager глобальный
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <limits>
#include <functional>
#include "Settings.h"
#include "AABB.h"

struct Character {
    glm::vec3 pos = glm::vec3(0.f, 20.f, 0.f);
    glm::vec3 vel = glm::vec3(0.f);
    float     eyeH = STAND_H;
    bool      onGround = false;
    bool      crouching = false;
    bool      sprinting = false;
};
extern Character player;

struct GunState {
    int   ammo = 12;
    float shootCooldown = 0.f;
    float recoilOffset = 0.f;
    float recoilTimer = 0.f;
    bool  reloading = false;
    bool  reloadFull = false;   // нужен Renderer.h
    float bobTimer = 0.f;    // нужен Renderer.h
    float adsProgress = 0.f;    // 0=бедро 1=прицел (ADS)
};
extern GunState gun;
extern float flashTimer;
extern int   fireAnimCounter;

struct BulletHole { glm::vec3 pos; float life; };
extern std::vector<BulletHole> bulletHoles;

inline float playerHP = 100.f;
inline float playerMaxHP = 100.f;

// ─── Физика игрока ───────────────────────────────────────
inline void updatePlayer(float dt)
{
    // Таймеры оружия
    if (gun.shootCooldown > 0) gun.shootCooldown -= dt;
    if (flashTimer > 0) flashTimer -= dt;

    // Recoil
    if (gun.recoilOffset > 0.f) {
        gun.recoilOffset -= dt * 0.25f;
        if (gun.recoilOffset < 0.f) gun.recoilOffset = 0.f;
    }

    // Reload trigger (R key handled in Input.h, здесь только таймер)

    // Bullet holes lifetime
    for (auto& bh : bulletHoles) bh.life -= dt;
    bulletHoles.erase(
        std::remove_if(bulletHoles.begin(), bulletHoles.end(),
            [](const BulletHole& b) { return b.life <= 0.f; }),
        bulletHoles.end());

    extern bool noclip;
    if (noclip) {
        // noclip: нет гравитации, нет коллизий
        player.vel = glm::vec3(0.f);
        player.onGround = false;
        return;
    }

    // Гравитация
    player.vel.y += GRAVITY * dt;

    // Желаемая высота приседания
    float targetEyeH = player.crouching ? CROUCH_H : STAND_H;
    player.eyeH += (targetEyeH - player.eyeH) * std::min(dt * 10.f, 1.f);

    // Движение
    player.pos += player.vel * dt;

    // Коллизия со стенами
    wallCollide(player.pos);

    // Коллизия с полом
    float gy = getGroundY(player.pos, 50.f);
    float floorY = (gy != std::numeric_limits<float>::lowest()) ? gy : -9999.f;

    if (player.pos.y <= floorY) {
        player.pos.y = floorY;
        player.vel.y = 0.f;
        player.onGround = true;
    }
    else {
        player.onGround = false;
    }

    // Смерть / респаун
    if (player.pos.y < -100.f) {
        player.pos = glm::vec3(0.f, 20.f, 0.f);
        player.vel = glm::vec3(0.f);
        playerHP = playerMaxHP;
    }
}

// ─── Выстрел ─────────────────────────────────────────────
// onHitEnemy(orig, dir, &distOut) — колбэк из main.cpp, вызывает enemyManager.rayHit
// Это нужно чтобы Player.h не зависел от Enemy.h (порядок include)
using ShootEnemyFn = std::function<int(const glm::vec3&, const glm::vec3&, float*)>;
inline ShootEnemyFn gShootEnemyFn; // инициализируется в main.cpp после загрузки

inline void doShoot(const glm::vec3& camPos, const glm::vec3& camFront,
    float fireRate, float recoilKick)
{
    if (gun.shootCooldown > 0 || gun.ammo <= 0 || gun.reloading) return;

    gun.ammo--;
    gun.shootCooldown = fireRate;
    gun.recoilOffset = recoilKick;
    flashTimer = 0.1f;
    fireAnimCounter++;

    // ── Сначала проверяем попадание по врагам ──────────────
    float enemyDist = 200.f;
    int hitIdx = -1;
    if (gShootEnemyFn)
        hitIdx = gShootEnemyFn(camPos, camFront, &enemyDist);

    // ── Потом пуля в геометрию карты ───────────────────────
    glm::vec3 hitPos;
    if (shootRay(camPos, camFront, hitPos)) {
        float wallDist = glm::length(hitPos - camPos);
        if (hitIdx < 0 || wallDist < enemyDist)
            bulletHoles.push_back({ hitPos, 5.f });
    }

    // Звук вызывается снаружи (в Input.h mouse_button_callback)
}