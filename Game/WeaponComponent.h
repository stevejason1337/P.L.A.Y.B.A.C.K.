#pragma once

// ============================================================
//  Game/WeaponComponent.h  —  ИГРА, трогаешь здесь
//  Компонент оружия. Вешается на объект оружия или игрока.
//  Использует PhysicsWorld::raycast для выстрела.
// ============================================================

#include "../Engine/Component.h"
#include "../Engine/InputSystem.h"
#include "../Engine/Physics.h"
#include "../Engine/AudioManager.h"
#include "../Engine/Renderer.h"

#include <string>
#include <functional>

// ── Описание оружия ────────────────────────────────────────
struct WeaponStats {
    std::string name        = "Pistol";
    int         maxAmmo     = 12;
    int         reserveAmmo = 60;
    float       damage      = 25.f;
    float       fireRate    = 0.15f;    // секунды между выстрелами
    float       reloadTime  = 1.5f;
    float       range       = 100.f;
    bool        isAuto      = false;    // true = держишь кнопку
    std::string shootSnd    = "pistol_shoot";
    std::string reloadSnd   = "pistol_reload";
    std::string emptySnd    = "empty_click";
};

// Колбек — вызывается при попадании (игровой код решает что делать)
using HitCallback = std::function<void(GameObject* hit, const glm::vec3& point, float damage)>;

class WeaponComponent : public Component {
public:
    WeaponStats stats;
    HitCallback onHit;          // установи из PlayerController или GameScene

    int   currentAmmo  = 0;
    int   reserveAmmo  = 0;
    bool  isReloading  = false;
    bool  equipped     = false;

    explicit WeaponComponent(const WeaponStats& s = {}) : stats(s) {}

    void start() override {
        currentAmmo  = stats.maxAmmo;
        reserveAmmo  = stats.reserveAmmo;

        AudioManager::get().load(stats.shootSnd,  "sounds/" + stats.shootSnd  + ".wav");
        AudioManager::get().load(stats.reloadSnd, "sounds/" + stats.reloadSnd + ".wav");
        AudioManager::get().load(stats.emptySnd,  "sounds/empty_click.wav");
    }

    void update(float dt) override {
        if (!equipped) return;

        fireCooldown  -= dt;
        reloadCooldown -= dt;

        if (reloadCooldown <= 0.f && isReloading) finishReload();

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
        if (isReloading) return;
        if (fireCooldown > 0.f)  return;

        if (currentAmmo <= 0) {
            AudioManager::get().play(stats.emptySnd);
            if (reserveAmmo > 0) startReload();
            return;
        }

        --currentAmmo;
        fireCooldown = stats.fireRate;
        AudioManager::get().play(stats.shootSnd);

        // Raycast из камеры
        auto& cam   = Renderer::get().camera;
        glm::vec3 from = cam.position;
        glm::vec3 to   = from + cam.front * stats.range;

        auto hit = PhysicsWorld::get().raycast(from, to);
        if (hit.hit && hit.body && onHit)
            onHit(hit.body->owner, hit.point, stats.damage);
    }

    void startReload() {
        isReloading    = true;
        reloadCooldown = stats.reloadTime;
        AudioManager::get().play(stats.reloadSnd);
    }

    // Сменить оружие
    void equip()   { equipped = true; }
    void unequip() { equipped = false; }

    bool isEmpty()    const { return currentAmmo == 0 && reserveAmmo == 0; }
    bool needsReload()const { return currentAmmo < stats.maxAmmo && reserveAmmo > 0; }

private:
    float fireCooldown   = 0.f;
    float reloadCooldown = 0.f;

    void finishReload() {
        int needed = stats.maxAmmo - currentAmmo;
        int take   = glm::min(needed, reserveAmmo);
        currentAmmo += take;
        reserveAmmo -= take;
        isReloading  = false;
    }
};

// ── WeaponManager — держит несколько оружий ────────────────
// Вешается на игрока как компонент
#include <vector>
#include <memory>

class WeaponManager : public Component {
public:
    int currentSlot = 0;

    void start() override {}

    void update(float dt) override {
        // Переключение слотов по 1,2,3 или колесо мыши
        auto& in = InputSystem::get();
        float scroll = in.getScrollDelta();
        if (scroll > 0.f)  switchSlot(-1);
        if (scroll < 0.f)  switchSlot(+1);

        // Обновить текущее оружие
        if (currentSlot < (int)weapons.size() && weapons[currentSlot])
            weapons[currentSlot]->update(dt);
    }

    WeaponComponent* addWeapon(const WeaponStats& stats) {
        auto* obj = owner->owner ?
            owner->owner->getComponent<WeaponComponent>() : nullptr;

        // Создаём компонент оружия прямо здесь как "дочерний" — упрощённо
        weapons.push_back(std::make_unique<WeaponComponent>(stats));
        auto* w = weapons.back().get();
        w->owner = owner;
        w->start();
        if (weapons.size() == 1) w->equip();
        return w;
    }

    WeaponComponent* getCurrent() {
        if (currentSlot < (int)weapons.size()) return weapons[currentSlot].get();
        return nullptr;
    }

    int  getAmmo()    const { return weapons.empty() ? 0 : weapons[currentSlot]->currentAmmo; }
    int  getReserve() const { return weapons.empty() ? 0 : weapons[currentSlot]->reserveAmmo; }

private:
    std::vector<std::unique_ptr<WeaponComponent>> weapons;

    void switchSlot(int dir) {
        if (weapons.empty()) return;
        if (getCurrent()) getCurrent()->unequip();
        currentSlot = (currentSlot + dir + (int)weapons.size()) % (int)weapons.size();
        getCurrent()->equip();
    }
};
