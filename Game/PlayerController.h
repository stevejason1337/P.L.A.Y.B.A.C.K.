#pragma once
// ============================================================
//  Game/PlayerController.h  —  ИГРА, трогаешь здесь
//  Компонент игрока. Движок про него ничего не знает.
// ============================================================

// ИСПРАВЛЕНО: правильные пути к движку
#include "../Engine/Component.h"
#include "../Engine/InputSystem.h"
#include "../Engine/Physics.h"
#include "../Engine/AudioManager.h"
#include "../Engine/Render/Renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

class PlayerController : public Component {
public:
    // ── Параметры ──────────────────────────────────────────
    float walkSpeed  = 5.f;
    float runSpeed   = 9.f;
    float jumpForce  = 6.f;
    float mouseSens  = 0.15f;

    // ── Состояние ──────────────────────────────────────────
    float health     = 100.f;
    bool  alive      = true;
    bool  isRunning  = false;
    bool  isCrouched = false;
    bool  isGrounded = false;
    float yaw        = 0.f;
    float pitch      = 0.f;

    void start() override {
        InputSystem::get().setCursorLocked(true);
        rb = owner->getComponent<RigidBody>();
        if (!rb)
            std::cerr << "[PlayerController] RigidBody not found!\n";

        AudioManager::get().load("footstep", "sounds/footstep.wav");
        AudioManager::get().load("jump",     "sounds/jump.wav");
        AudioManager::get().load("land",     "sounds/land.wav");
    }

    void update(float dt) override {
        if (!alive) return;

        _handleCamera();
        _handleMovement(dt);
        _handleJump();
        _syncCamera();

        // Footstep
        footstepTimer -= dt;
        if (isGrounded && _isMoving() && footstepTimer <= 0.f) {
            AudioManager::get().play("footstep");
            footstepTimer = isRunning ? 0.35f : 0.55f;
        }
    }

    void takeDamage(float dmg) {
        if (!alive) return;
        health -= dmg;
        if (health <= 0.f) { health = 0.f; _die(); }
    }

    glm::vec3 getEyePosition() const {
        return owner->position + glm::vec3(0.f, eyeHeight, 0.f);
    }

    glm::vec3 getForward() const {
        return Renderer::get().camera.front;
    }

private:
    RigidBody* rb           = nullptr;
    float      eyeHeight    = 1.7f;
    float      footstepTimer = 0.f;
    bool       wasGrounded  = false;

    void _handleCamera() {
        glm::vec2 delta = InputSystem::get().getMouseDelta();
        yaw   += delta.x * mouseSens;
        pitch -= delta.y * mouseSens;
        pitch  = glm::clamp(pitch, -89.f, 89.f);
    }

    void _handleMovement(float dt) {
        auto& input = InputSystem::get();

        isRunning  = input.isKeyDown(Key::LShift) && !isCrouched;
        isCrouched = input.isKeyDown(Key::LCtrl);

        float speed = isCrouched ? walkSpeed * 0.5f
                    : isRunning  ? runSpeed
                    :              walkSpeed;

        glm::vec3 front = _getFlatForward();
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.f,1.f,0.f)));
        glm::vec3 move  = glm::vec3(0.f);

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

        if (isGrounded && !wasGrounded)
            AudioManager::get().play("land");
        wasGrounded = isGrounded;
    }

    void _handleJump() {
        if (InputSystem::get().isKeyPressed(Key::Space) && isGrounded && rb) {
            rb->applyImpulse(glm::vec3(0.f, jumpForce, 0.f));
            AudioManager::get().play("jump");
        }
    }

    void _syncCamera() {
        auto& cam = Renderer::get().camera;
        cam.position = getEyePosition();

        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cam.front = glm::normalize(front);

        if (rb) owner->position = rb->getPosition();
    }

    glm::vec3 _getFlatForward() const {
        return glm::normalize(glm::vec3(
            cos(glm::radians(yaw)),
            0.f,
            sin(glm::radians(yaw))
        ));
    }

    bool _isMoving() const {
        auto& input = InputSystem::get();
        return input.isKeyDown(Key::W) || input.isKeyDown(Key::A)
            || input.isKeyDown(Key::S) || input.isKeyDown(Key::D);
    }

    void _die() {
        alive = false;
        InputSystem::get().setCursorLocked(false);
        std::cout << "[Player] Dead\n";
    }
};
