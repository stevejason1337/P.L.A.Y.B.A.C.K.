#pragma once

// ============================================================
//  Game/GameScene.h  —  ИГРА, трогаешь здесь
//  Главная сцена игры. Создаёт все объекты, связывает их.
//  Наследует GameBase — движок вызывает onStart/onUpdate/onRenderUI.
// ============================================================

#include "../Engine/Core.h"
#include "../Engine/MeshRenderer.h"
#include "../Engine/AnimatedModel.h"
#include "../Engine/Physics.h"
#include "../Engine/AudioManager.h"
#include "../Engine/InputSystem.h"
#include "../Engine/Renderer.h"

#include "PlayerController.h"
#include "WeaponComponent.h"
#include "EnemyController.h"
#include "HudComponent.h"

// ImGui
#include "../imgui.h"
#include "../imgui_impl_glfw.h"
#include "../imgui_impl_opengl3.h"

#include <vector>

class GameScene : public GameBase {
public:
    // ── Публичное состояние ────────────────────────────────
    int   score       = 0;
    bool  paused      = false;

private:
    // Указатели на ключевые объекты (владеет Scene)
    GameObject*       playerObj   = nullptr;
    PlayerController* player      = nullptr;
    WeaponManager*    weapons     = nullptr;
    HudComponent*     hud         = nullptr;

    std::vector<GameObject*> enemies;
    std::vector<float>       enemyDeathTimers;   // таймер удаления трупов

    // ── ImGui ──────────────────────────────────────────────
    GLFWwindow* win = nullptr;

public:
    // ═══════════════════════════════════════════════════════
    //  onStart — создать всю сцену
    // ═══════════════════════════════════════════════════════
    void onStart() override {
        win = Renderer::get().getWindow();

        // ImGui init
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(win, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        // Настройка рендерера
        Renderer::get().sun.direction = glm::normalize(glm::vec3(-0.4f, -1.f, -0.6f));
        Renderer::get().sun.intensity = 1.2f;
        Renderer::get().clearColor    = glm::vec4(0.45f, 0.6f, 0.75f, 1.f);

        // Фоновая музыка
        AudioManager::get().load("music", "sounds/ambient.wav");
        AudioManager::get().play("music", true);
        AudioManager::get().setVolume("music", 0.4f);

        spawnPlayer();
        spawnLevel();
        spawnEnemies();
        spawnHud();

        // ── Связать колбеки ───────────────────────────────
        // Попадание по врагу
        if (auto* wc = weapons ? weapons->getCurrent() : nullptr) {
            wc->onHit = [this](GameObject* hit, const glm::vec3& point, float dmg) {
                onBulletHit(hit, point, dmg);
            };
        }
    }

    // ═══════════════════════════════════════════════════════
    //  onUpdate — логика поверх компонентов
    // ═══════════════════════════════════════════════════════
    void onUpdate(float dt) override {
        handlePause();
        if (paused) return;

        // Синхронизировать оружие с камерой (HUD данные)
        if (weapons && weapons->getCurrent()) {
            auto* wc = weapons->getCurrent();
            // Колбек переустановить если сменили оружие
            wc->onHit = [this](GameObject* hit, const glm::vec3& point, float dmg) {
                onBulletHit(hit, point, dmg);
            };
        }

        // Убрать мёртвых врагов через 3 секунды
        for (int i = (int)enemies.size() - 1; i >= 0; i--) {
            auto* ec = enemies[i]->getComponent<EnemyController>();
            if (ec && ec->isDead()) {
                enemyDeathTimers[i] -= dt;
                if (enemyDeathTimers[i] <= 0.f) {
                    scene->destroy(enemies[i]);
                    enemies.erase(enemies.begin() + i);
                    enemyDeathTimers.erase(enemyDeathTimers.begin() + i);
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════
    //  onRenderUI — ImGui / HUD
    // ═══════════════════════════════════════════════════════
    void onRenderUI() override {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (hud) hud->renderUI();

        if (paused) renderPauseMenu();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void onShutdown() override {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

private:
    // ─────────────────────────────────────────────────────
    //  Спавн игрока
    // ─────────────────────────────────────────────────────
    void spawnPlayer() {
        playerObj = scene->createObject("Player");
        playerObj->position = glm::vec3(0.f, 2.f, 0.f);

        // RigidBody — двигатель физики
        RigidBodyDesc rbDesc;
        rbDesc.mass        = 70.f;
        rbDesc.boxHalfSize = glm::vec3(0.35f, 0.9f, 0.35f);   // capsule приближение
        rbDesc.noRotation  = true;
        rbDesc.friction    = 0.8f;
        auto* rb = playerObj->addComponent<RigidBody>(rbDesc);
        PhysicsWorld::get().registerBody(rb, playerObj->position);

        // PlayerController
        player = playerObj->addComponent<PlayerController>();

        // Оружия
        weapons = playerObj->addComponent<WeaponManager>();

        WeaponStats pistol;
        pistol.name = "Pistol"; pistol.maxAmmo = 12; pistol.reserveAmmo = 60;
        pistol.damage = 25.f;   pistol.fireRate = 0.2f;
        pistol.shootSnd = "pistol_shoot"; pistol.reloadSnd = "pistol_reload";
        weapons->addWeapon(pistol);

        WeaponStats smg;
        smg.name = "SMG"; smg.maxAmmo = 30; smg.reserveAmmo = 120;
        smg.damage = 15.f; smg.fireRate = 0.08f; smg.isAuto = true;
        smg.shootSnd = "smg_shoot"; smg.reloadSnd = "smg_reload";
        weapons->addWeapon(smg);

        AudioManager::get().load("pistol_shoot", "sounds/pistol_shoot.wav");
        AudioManager::get().load("pistol_reload","sounds/pistol_reload.wav");
        AudioManager::get().load("smg_shoot",    "sounds/smg_shoot.wav");
        AudioManager::get().load("smg_reload",   "sounds/smg_reload.wav");
        AudioManager::get().load("empty_click",  "sounds/empty_click.wav");
    }

    // ─────────────────────────────────────────────────────
    //  Спавн уровня (пол, стены)
    // ─────────────────────────────────────────────────────
    void spawnLevel() {
        // Пол
        auto* floor = scene->createObject("Floor");
        floor->position = glm::vec3(0.f, -0.5f, 0.f);
        floor->scale    = glm::vec3(50.f, 1.f, 50.f);
        floor->addComponent<MeshRenderer>("models/cube.obj");

        RigidBodyDesc floorDesc;
        floorDesc.mass        = 0.f;    // статический
        floorDesc.boxHalfSize = glm::vec3(25.f, 0.5f, 25.f);
        auto* frb = floor->addComponent<RigidBody>(floorDesc);
        PhysicsWorld::get().registerBody(frb, floor->position);

        // Стена 1
        spawnWall({-10.f, 1.5f, 0.f}, {0.5f, 3.f, 10.f});
        spawnWall({ 10.f, 1.5f, 0.f}, {0.5f, 3.f, 10.f});
        spawnWall({0.f,   1.5f,-10.f}, {10.f, 3.f, 0.5f});

        // Загрузить карту из файла (опционально)
        // mapLoader.load("maps/level1.map", *scene);
    }

    void spawnWall(const glm::vec3& pos, const glm::vec3& halfSize) {
        auto* wall = scene->createObject("Wall");
        wall->position = pos;
        wall->scale    = halfSize * 2.f;
        wall->addComponent<MeshRenderer>("models/cube.obj");

        RigidBodyDesc wd;
        wd.mass        = 0.f;
        wd.boxHalfSize = halfSize;
        auto* wrb = wall->addComponent<RigidBody>(wd);
        PhysicsWorld::get().registerBody(wrb, pos);
    }

    // ─────────────────────────────────────────────────────
    //  Спавн врагов
    // ─────────────────────────────────────────────────────
    void spawnEnemies() {
        std::vector<glm::vec3> positions = {
            {5.f,  1.f,  5.f},
            {-8.f, 1.f,  3.f},
            {3.f,  1.f, -6.f},
            {-5.f, 1.f, -8.f},
        };

        for (auto& pos : positions)
            spawnEnemy(pos);
    }

    void spawnEnemy(const glm::vec3& pos) {
        auto* obj = scene->createObject("Enemy");
        obj->position = pos;

        EnemyStats es;
        es.health = 100.f; es.speed = 3.f; es.runSpeed = 5.f;
        es.detectionRange = 15.f; es.attackRange = 1.8f; es.attackDamage = 12.f;

        RigidBodyDesc rd;
        rd.mass = 60.f; rd.boxHalfSize = glm::vec3(0.3f, 0.9f, 0.3f);
        rd.noRotation = true;
        auto* erb = obj->addComponent<RigidBody>(rd);
        PhysicsWorld::get().registerBody(erb, pos);

        obj->addComponent<MeshRenderer>("models/enemy.obj");

        auto* ec = obj->addComponent<EnemyController>(es);
        ec->setTarget(playerObj);
        ec->onAttackPlayer = [this](float dmg) {
            if (player) player->takeDamage(dmg);
        };

        enemies.push_back(obj);
        enemyDeathTimers.push_back(3.f);
    }

    // ─────────────────────────────────────────────────────
    //  HUD
    // ─────────────────────────────────────────────────────
    void spawnHud() {
        auto* hudObj = scene->createObject("HUD");
        hud = hudObj->addComponent<HudComponent>();

        if (player && weapons) {
            hud->data.health      = &player->health;
            hud->data.playerAlive = &player->alive;
            hud->data.score       = &score;
            // ammo обновляется через getCurrent() каждый кадр
            // (WeaponManager владеет данными)
        }
    }

    // ─────────────────────────────────────────────────────
    //  Обработка попадания пули
    // ─────────────────────────────────────────────────────
    void onBulletHit(GameObject* hit, const glm::vec3& point, float dmg) {
        if (!hit) return;
        if (auto* ec = hit->getComponent<EnemyController>()) {
            ec->takeDamage(dmg);
            if (ec->isDead()) score += 100;
        }
        // TODO: BulletDecal, BloodFX через компоненты
    }

    // ─────────────────────────────────────────────────────
    //  Пауза
    // ─────────────────────────────────────────────────────
    void handlePause() {
        if (InputSystem::get().isKeyPressed(Key::Escape)) {
            paused = !paused;
            InputSystem::get().setCursorLocked(!paused);
        }
    }

    void renderPauseMenu() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f - 100.f,
                                 io.DisplaySize.y * 0.5f - 80.f});
        ImGui::SetNextWindowSize({200.f, 160.f});
        ImGui::Begin("##pause", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::TextColored({1.f,1.f,0.f,1.f}, "    PAUSED");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Resume", {180.f, 36.f})) {
            paused = false;
            InputSystem::get().setCursorLocked(true);
        }
        ImGui::Spacing();
        if (ImGui::Button("Quit", {180.f, 36.f})) {
            glfwSetWindowShouldClose(win, true);
        }
        ImGui::End();
    }
};
