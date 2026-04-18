#pragma once
// ============================================================
//  Game/WeaponComponent.h  —  ИГРА, трогаешь здесь
// ============================================================

// ИСПРАВЛЕНО: правильные пути к движку
#include "../Engine/Component.h"
#include "../Engine/InputSystem.h"
#include "../Engine/Physics.h"
#include "../Engine/AudioManager.h"
#include "../Engine/Render/Renderer.h"

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <iostream>

// ── Описание оружия ────────────────────────────────────────
struct WeaponStats {
    std::string name        = "Pistol";
    int         maxAmmo     = 12;
    int         reserveAmmo = 60;
    float       damage      = 25.f;
    float       fireRate    = 0.15f;
    float       reloadTime  = 1.5f;
    float       range       = 100.f;
    bool        isAuto      = false;
    std::string shootSnd    = "pistol_shoot";
    std::string reloadSnd   = "pistol_reload";
    std::string emptySnd    = "empty_click";
};

using HitCallback = std::function<void(GameObject* hit, const glm::vec3& point, float damage)>;

class WeaponComponent : public Component {
public:
    WeaponStats stats;
    HitCallback onHit;

    int   currentAmmo  = 0;
    int   reserveAmmo  = 0;
    bool  isReloading  = false;
    bool  equipped     = false;

    explicit WeaponComponent(const WeaponStats& s = {}) : stats(s) {}

    void start() override {
        currentAmmo = stats.maxAmmo;
        reserveAmmo = stats.reserveAmmo;
    }

    void update(float dt) override {
        if (!equipped) return;

        fireCooldown   -= dt;
        reloadCooldown -= dt;

        if (reloadCooldown <= 0.f && isReloading)
            _finishReload();

        auto& input = InputSystem::get();
        bool shootInput = stats.isAuto
            ? input.isMouseDown(Mouse::Left)
            : input.isMousePressed(Mouse::Left);

        if (shootInput) tryShoot();

        if (input.isKeyPressed(Key::R) && !isReloading
            && currentAmmo < stats.maxAmmo && reserveAmmo > 0)
            startReload();
    }

    void tryShoot() {
        if (isReloading || fireCooldown > 0.f) return;

        if (currentAmmo <= 0) {
            AudioManager::get().play(stats.emptySnd);
            if (reserveAmmo > 0) startReload();
            return;
        }

        --currentAmmo;
        fireCooldown = stats.fireRate;
        AudioManager::get().play(stats.shootSnd);

        // Raycast из камеры
        auto& cam    = Renderer::get().camera;
        glm::vec3 from = cam.position;
        glm::vec3 to   = from + cam.front * stats.range;

        auto hit = PhysicsWorld::get().raycast(from, to);
        if (hit.hit && hit.body && onHit)
            onHit(hit.body->owner, hit.point, stats.damage);
    }

    void startReload() {
        if (isReloading) return;
        isReloading    = true;
        reloadCooldown = stats.reloadTime;
        AudioManager::get().play(stats.reloadSnd);
    }

    void equip()   { equipped = true;  }
    void unequip() { equipped = false; }

    bool isEmpty()     const { return currentAmmo == 0 && reserveAmmo == 0; }
    bool needsReload() const { return currentAmmo < stats.maxAmmo && reserveAmmo > 0; }

private:
    float fireCooldown   = 0.f;
    float reloadCooldown = 0.f;

    void _finishReload() {
        int needed = stats.maxAmmo - currentAmmo;
        int take   = glm::min(needed, reserveAmmo);
        currentAmmo += take;
        reserveAmmo -= take;
        isReloading  = false;
    }
};

// ── WeaponManager ─────────────────────────────────────────
class WeaponManager : public Component {
public:
    int currentSlot = 0;

    void start()           override {}
    void update(float dt)  override {
        auto& in = InputSystem::get();
        float scroll = in.getScrollDelta();
        if (scroll > 0.f) _switchSlot(-1);
        if (scroll < 0.f) _switchSlot(+1);

        // Цифровые клавиши
        if (in.isKeyPressed(Key::Alpha1)) _switchToSlot(0);
        if (in.isKeyPressed(Key::Alpha2)) _switchToSlot(1);
        if (in.isKeyPressed(Key::Alpha3)) _switchToSlot(2);

        if (currentSlot < (int)weapons.size() && weapons[currentSlot])
            weapons[currentSlot]->update(dt);
    }

    WeaponComponent* addWeapon(const WeaponStats& stats) {
        weapons.push_back(std::make_unique<WeaponComponent>(stats));
        auto* w   = weapons.back().get();
        w->owner  = owner;
        w->start();
        if (weapons.size() == 1) w->equip();
        return w;
    }

    WeaponComponent* getCurrent() {
        if (currentSlot < (int)weapons.size())
            return weapons[currentSlot].get();
        return nullptr;
    }

    int getAmmo()    const { return (currentSlot < (int)weapons.size()) ? weapons[currentSlot]->currentAmmo  : 0; }
    int getReserve() const { return (currentSlot < (int)weapons.size()) ? weapons[currentSlot]->reserveAmmo  : 0; }

private:
    std::vector<std::unique_ptr<WeaponComponent>> weapons;

    void _switchSlot(int dir) {
        if (weapons.empty()) return;
        if (getCurrent()) getCurrent()->unequip();
        currentSlot = (currentSlot + dir + (int)weapons.size()) % (int)weapons.size();
        if (getCurrent()) getCurrent()->equip();
    }

    void _switchToSlot(int slot) {
        if (slot < 0 || slot >= (int)weapons.size()) return;
        if (slot == currentSlot) return;
        if (getCurrent()) getCurrent()->unequip();
        currentSlot = slot;
        if (getCurrent()) getCurrent()->equip();
    }
};
