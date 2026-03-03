#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Player.h"
#include "WeaponManager.h"
#include "Soundmanager.h"
#include "Console.h"

extern glm::vec3 camFront;
extern glm::vec3 camUp;
extern bool      playerMoving;

inline float yaw = -90.f;
inline float pitch = 0.f;
inline float lastMouseX = 640.f;
inline float lastMouseY = 360.f;
inline bool  firstMouse = true;

inline void processMovement(GLFWwindow* w)
{
    if (console.open) return;
    player.crouching = (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
    player.sprinting = (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) && player.onGround && !player.crouching;
    float speed = player.sprinting ? SPRINT_SPEED : (player.crouching ? CROUCH_SPEED : WALK_SPEED);
    glm::vec3 flat = glm::normalize(glm::vec3(camFront.x, 0.f, camFront.z));
    glm::vec3 right = glm::normalize(glm::cross(flat, camUp));
    glm::vec3 dir(0.f);
    playerMoving = false;
    extern bool noclip;
    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) { dir += noclip ? camFront : flat; playerMoving = true; }
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) { dir -= noclip ? camFront : flat; playerMoving = true; }
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) { dir -= right; playerMoving = true; }
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) { dir += right; playerMoving = true; }
    if (noclip) {
        if (glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS) { dir += glm::vec3(0, 1, 0); playerMoving = true; }
        if (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) { dir -= glm::vec3(0, 1, 0); playerMoving = true; }
    }
    if (playerMoving) { float len = glm::length(dir); if (len > 0.f) player.pos += (dir / len) * speed * 0.016f; }
}

inline void mouse_callback(GLFWwindow*, double xpos, double ypos)
{
    if (console.open) return;
    if (firstMouse) { lastMouseX = (float)xpos; lastMouseY = (float)ypos; firstMouse = false; }
    float dx = ((float)xpos - lastMouseX) * MOUSE_SENS;
    float dy = (lastMouseY - (float)ypos) * MOUSE_SENS;
    lastMouseX = (float)xpos; lastMouseY = (float)ypos;
    yaw += dx; pitch += dy;
    if (pitch > PITCH_LIM) pitch = PITCH_LIM;
    if (pitch < -PITCH_LIM) pitch = -PITCH_LIM;
    glm::vec3 front;
    front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front.y = sinf(glm::radians(pitch));
    front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    camFront = glm::normalize(front);
}

inline void key_callback(GLFWwindow* w, int key, int, int action, int)
{
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) { console.toggle(); return; }
    if (console.open) { console.keyInput(key, action); return; }
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_SPACE && player.onGround) { player.vel.y = JUMP_FORCE; player.onGround = false; }
        if (key == GLFW_KEY_1) weaponManager.pressSlot(0);
        if (key == GLFW_KEY_2) weaponManager.pressSlot(1);
        if (key == GLFW_KEY_3) weaponManager.pressSlot(2);
        // Перезарядка — звук запускается тут же
        if (key == GLFW_KEY_R && !gun.reloading && gun.ammo < weaponManager.activeDef().maxAmmo) {
            gun.reloading = true;
            gun.reloadFull = (gun.ammo == 0);
            soundManager.playReload(weaponManager.current);
        }
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    }
}

inline void mouse_button_callback(GLFWwindow*, int button, int action, int)
{
    if (console.open) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Во время перезарядки — полная тишина
        if (gun.reloading) return;
        // Пустой магазин — клик
        if (gun.ammo <= 0) { soundManager.playEmpty(); return; }
        // Ещё не остыл — тишина
        if (gun.shootCooldown > 0) return;
        // Реальный выстрел + звук
        const auto& def = weaponManager.activeDef();
        doShoot(player.pos + glm::vec3(0, player.eyeH, 0), camFront, def.fireRate, def.recoilKick);
        soundManager.playShot(weaponManager.current);
    }
}

inline void char_callback(GLFWwindow*, unsigned int c) { console.charInput(c); }