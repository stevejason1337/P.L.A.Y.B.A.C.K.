#pragma once

// ============================================================
//  Game/PlayerController.h  —  ИГРА, трогаешь здесь
//  Компонент игрока. Движок про него ничего не знает.
//  Добавляется так: playerObj->addComponent<PlayerController>();
// ============================================================

#include "../Engine/Component.h"
#include "../Engine/InputSystem.h"
#include "../Engine/Physics.h"
#include "../Engine/AudioManager.h"
#include "../Engine/Renderer.h"     // для CameraData

class PlayerController : public Component {
public:
    // ── Параметры ──────────────────────────────────────────
    float walkSpeed  = 5.f;
    float runSpeed   = 9.f;
    float jumpForce  = 6.f;
    float mouseSens  = 0.15f;
    float health     = 100.f;
    bool  alive      = true;

    // ── Состояние ──────────────────────────────────────────
    bool  isRunning  = false;
    bool  isCrouched = false;
    bool  isGrounded = false;
    float yaw        = 0.f;
    float pitch      = 0.f;

    void start() override {
        InputSystem::get().setCursorLocked(true);

        rb = owner->getComponent<RigidBody>();
        if (!rb) std::cerr << "[PlayerController] RigidBody not found!\n";

        // Загрузить звуки
        AudioManager::get().load("footstep", "sounds/footstep.wav");
        AudioManager::get().load("jump",     "sounds/jump.wav");
        AudioManager::get().load("land",     "sounds/land.wav");
    }

    void update(float dt) override {
        if (!alive) return;

        handleCamera();
        handleMovement(dt);
        handleJump();
        syncCamera();

        // Footstep звук
        footstepTimer -= dt;
        if (isGrounded && isMoving() && footstepTimer <= 0.f) {
            AudioManager::get().play("footstep");
            footstepTimer = isRunning ? 0.35f : 0.55f;
        }
    }

    void takeDamage(float dmg) {
        if (!alive) return;
        health -= dmg;
        if (health <= 0.f) { health = 0.f; die(); }
    }

    glm::vec3 getEyePosition() const {
        return owner->position + glm::vec3(0.f, eyeHeight, 0.f);
    }

    glm::vec3 getForward() const {
        return Renderer::get().camera.front;
    }

private:
    RigidBody* rb          = nullptr;
    float      eyeHeight   = 1.7f;
    float      footstepTimer = 0.f;
    bool       wasGrounded   = false;

    void handleCamera() {
        auto& input = InputSystem::get();
        glm::vec2 delta = input.getMouseDelta();
        yaw   += delta.x * mouseSens;
        pitch -= delta.y * mouseSens;
        pitch  = glm::clamp(pitch, -89.f, 89.f);
    }

    void handleMovement(float dt) {
        auto& input = InputSystem::get();

        isRunning  = input.isKeyDown(Key::LShift) && !isCrouched;
        isCrouched = input.isKeyDown(Key::LCtrl);

        float speed = isCrouched ? walkSpeed * 0.5f
                    : isRunning  ? runSpeed
                    :              walkSpeed;

        glm::vec3 front  = getFlatForward();
        glm::vec3 right  = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));
        glm::vec3 move   = glm::vec3(0.f);

        if (input.isKeyDown(Key::W)) move += front;
        if (input.isKeyDown(Key::S)) move -= front;
        if (input.isKeyDown(Key::A)) move -= right;
        if (input.isKeyDown(Key::D)) move += right;

        if (rb) {
            glm::vec3 vel = rb->getLinearVelocity();
            if (glm::length(move) > 0.001f) {
                move = glm::normalize(move) * speed;
                rb->setLinearVelocity({move.x, vel.y, move.z});
            } else {
                rb->setLinearVelocity({vel.x * 0.85f, vel.y, vel.z * 0.85f});
            }
            isGrounded = rb->isGrounded;
        }

        // Приземление
        if (isGrounded && !wasGrounded)
            AudioManager::get().play("land");
        wasGrounded = isGrounded;
    }

    void handleJump() {
        if (InputSystem::get().isKeyPressed(Key::Space) && isGrounded && rb) {
            rb->applyImpulse(glm::vec3(0.f, jumpForce, 0.f));
            AudioManager::get().play("jump");
        }
    }

    void syncCamera() {
        // Позиция камеры = позиция игрока + высота глаз
        auto& cam = Renderer::get().camera;
        cam.position = getEyePosition();

        // Направление взгляда из yaw/pitch
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cam.front = glm::normalize(front);

        // Синхронизировать позицию GameObject с RigidBody
        if (rb) owner->position = rb->getPosition();
    }

    glm::vec3 getFlatForward() const {
        glm::vec3 f;
        f.x = cos(glm::radians(yaw));
        f.y = 0.f;
        f.z = sin(glm::radians(yaw));
        return glm::normalize(f);
    }

    bool isMoving() const {
        auto& input = InputSystem::get();
        return input.isKeyDown(Key::W) || input.isKeyDown(Key::A)
            || input.isKeyDown(Key::S) || input.isKeyDown(Key::D);
    }

    void die() {
        alive = false;
        std::cout << "[Player] Dead\n";
        // В игровом коде подпишись на это событие (колбек или polling)
    }
};
