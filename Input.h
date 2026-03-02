#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Settings.h"
#include "Player.h"
#include "WeaponManager.h"
#include "Console.h"

inline glm::vec3 camFront = glm::vec3(0, 0, -1);
inline glm::vec3 camUp = glm::vec3(0, 1, 0);
inline float yaw = -90.f, pitch = 0.f;
inline float lastX = SCR_WIDTH / 2.f, lastY = SCR_HEIGHT / 2.f;
inline bool  firstMouse = true;

inline void processMovement(GLFWwindow* w)
{
    if (console.open) return;

    player.crouching = (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
    player.sprinting = (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        && !player.crouching && player.onGround;

    float speed = player.sprinting ? SPRINT_SPEED
        : player.crouching ? CROUCH_SPEED : WALK_SPEED;

    glm::vec3 flat = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
    glm::vec3 right = glm::normalize(glm::cross(flat, camUp));
    glm::vec3 dir(0);

    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) dir += flat;
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) dir -= flat;
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) dir -= right;
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) dir += right;

    if (glm::length(dir) > 0.001f) dir = glm::normalize(dir);

    if (noclip) {
        player.vel = dir * speed * 2.f;
        player.vel.y = 0.f;
        if (glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS)        player.vel.y = speed;
        if (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) player.vel.y = -speed;
        player.onGround = false;
    }
    else {
        player.vel.x = dir.x * speed;
        player.vel.z = dir.z * speed;
        if (glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS
            && player.onGround && !player.crouching)
        {
            player.vel.y = JUMP_FORCE; player.onGround = false;
        }
    }
}

inline void mouse_callback(GLFWwindow*, double xIn, double yIn)
{
    if (console.open) return;
    float x = (float)xIn, y = (float)yIn;
    if (firstMouse) { lastX = x; lastY = y; firstMouse = false; }
    yaw += (x - lastX) * MOUSE_SENS;
    pitch = glm::clamp(pitch + (lastY - y) * MOUSE_SENS, -PITCH_LIM, PITCH_LIM);
    lastX = x; lastY = y;
    glm::vec3 f;
    f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    f.y = sin(glm::radians(pitch));
    f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    camFront = glm::normalize(f);
}

struct Renderer;
extern Renderer* gRenderer;
void onWeaponSwitchRenderer();
void shootWithEnemyCheck(const glm::vec3&, const glm::vec3&, float, float);

inline void char_callback(GLFWwindow*, unsigned int c)
{
    console.charInput(c);
}

inline void key_callback(GLFWwindow* w, int key, int, int action, int)
{
    // ` = toggle console
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        console.toggle();
        if (console.open) glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        else { glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED); firstMouse = true; }
        return;
    }

    if (console.open) { console.keyInput(key, action); return; }

    // ESC removed — use console "quit" command to exit

    if (key == GLFW_KEY_R && action == GLFW_PRESS)
        doReload(weaponManager.activeDef().maxAmmo);

    if (action == GLFW_PRESS && !gun.reloading) {
        int slot = -1;
        if (key == GLFW_KEY_1) slot = 0;
        if (key == GLFW_KEY_2) slot = 1;
        if (key == GLFW_KEY_3) slot = 2;
        if (key == GLFW_KEY_4) slot = 3;
        if (slot >= 0) { weaponManager.pressSlot(slot); onWeaponSwitchRenderer(); }
    }
}

inline void mouse_button_callback(GLFWwindow*, int button, int action, int)
{
    if (console.open) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        glm::vec3 cp = player.pos + glm::vec3(0, player.eyeH, 0);
        const WeaponDef& def = weaponManager.activeDef();
        shootWithEnemyCheck(cp, camFront, def.fireRate, def.recoilKick);
    }
}

inline void framebuffer_size_callback(GLFWwindow*, int w, int h)
{
    glViewport(0, 0, w, h);
}