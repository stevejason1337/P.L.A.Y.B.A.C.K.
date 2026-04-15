#pragma once

// ============================================================
//  Game/EnemyController.h  —  ИГРА, трогаешь здесь
//  Компонент врага. Простая AI: Idle → Chase → Attack.
// ============================================================

#include "../Engine/Component.h"
#include "../Engine/Physics.h"
#include "../Engine/AudioManager.h"
#include "../Engine/AnimatedModel.h"

#include <glm/glm.hpp>
#include <functional>

enum class EnemyState { Idle, Alert, Chase, Attack, Dead };

struct EnemyStats {
    float health       = 100.f;
    float speed        = 3.f;
    float runSpeed     = 5.5f;
    float detectionRange = 18.f;
    float attackRange    = 2.f;
    float attackDamage   = 15.f;
    float attackCooldown = 1.2f;
};

// Колбек — игра сама решает что делать при атаке врага
using EnemyAttackCallback = std::function<void(float damage)>;

class EnemyController : public Component {
public:
    EnemyStats          stats;
    EnemyAttackCallback onAttackPlayer;   // установи из GameScene

    float health;
    EnemyState state  = EnemyState::Idle;

    explicit EnemyController(const EnemyStats& s = {}) : stats(s), health(s.health) {}

    void start() override {
        rb    = owner->getComponent<RigidBody>();
        anim  = owner->getComponent<AnimatedModel>();

        AudioManager::get().load("enemy_alert",  "sounds/enemy_alert.wav");
        AudioManager::get().load("enemy_attack", "sounds/enemy_attack.wav");
        AudioManager::get().load("enemy_die",    "sounds/enemy_die.wav");

        if (anim) anim->play("idle");
    }

    void update(float dt) override {
        if (state == EnemyState::Dead) return;
        if (!playerTarget) return;

        float dist = glm::distance(owner->position, playerTarget->position);
        attackTimer -= dt;

        switch (state) {
            case EnemyState::Idle:
                if (dist < stats.detectionRange) {
                    state = EnemyState::Alert;
                    AudioManager::get().play("enemy_alert");
                    if (anim) anim->play("walk");
                }
                break;

            case EnemyState::Alert:
                state = EnemyState::Chase;
                break;

            case EnemyState::Chase:
                if (dist <= stats.attackRange) {
                    state = EnemyState::Attack;
                    if (anim) anim->play("attack", false);
                } else {
                    moveToward(playerTarget->position, stats.runSpeed, dt);
                }
                break;

            case EnemyState::Attack:
                if (attackTimer <= 0.f) {
                    doAttack();
                    attackTimer = stats.attackCooldown;
                }
                if (dist > stats.attackRange + 1.f) {
                    state = EnemyState::Chase;
                    if (anim) anim->play("run");
                }
                break;

            default: break;
        }
    }

    void setTarget(GameObject* player) { playerTarget = player; }

    void takeDamage(float dmg) {
        if (state == EnemyState::Dead) return;
        health -= dmg;
        if (health <= 0.f) die();
        else if (state == EnemyState::Idle) {
            state = EnemyState::Chase;
            if (anim) anim->play("run");
        }
    }

    bool isDead() const { return state == EnemyState::Dead; }

private:
    RigidBody*    rb            = nullptr;
    AnimatedModel* anim         = nullptr;
    GameObject*   playerTarget  = nullptr;
    float         attackTimer   = 0.f;

    void moveToward(const glm::vec3& target, float speed, float dt) {
        glm::vec3 dir = target - owner->position;
        dir.y = 0.f;
        if (glm::length(dir) < 0.1f) return;
        dir = glm::normalize(dir);

        if (rb) {
            glm::vec3 vel = rb->getLinearVelocity();
            rb->setLinearVelocity({dir.x * speed, vel.y, dir.z * speed});
            owner->position = rb->getPosition();
        } else {
            owner->position += dir * speed * dt;
        }

        // Повернуть к игроку
        float angle = atan2(dir.x, dir.z);
        owner->rotation = glm::quat(glm::vec3(0.f, angle, 0.f));
    }

    void doAttack() {
        AudioManager::get().play("enemy_attack");
        if (onAttackPlayer) onAttackPlayer(stats.attackDamage);
    }

    void die() {
        state  = EnemyState::Dead;
        health = 0.f;
        AudioManager::get().play("enemy_die");
        if (anim) anim->play("die", false);
        if (rb)   rb->setLinearVelocity(glm::vec3(0.f));
        // Объект удалится через 3 сек — GameScene следит за этим
    }
};
