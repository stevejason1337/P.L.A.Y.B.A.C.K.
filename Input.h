#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Settings.h"
#include "Player.h"
#include "WeaponManager.h"

inline glm::vec3 camFront = glm::vec3(0, 0, -1);
inline glm::vec3 camUp = glm::vec3(0, 1, 0);
inline float yaw = -90.f, pitch = 0.f;
inline float lastX = SCR_WIDTH / 2.f, lastY = SCR_HEIGHT / 2.f;
inline bool  firstMouse = true;

inline void processMovement(GLFWwindow* w)
{
    player.crouching = (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
    player.sprinting = (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        && !player.crouching && player.onGround;

    float speed = player.sprinting ? SPRINT_SPEED : player.crouching ? CROUCH_SPEED : WALK_SPEED;
    glm::vec3 flat = glm::normalize(glm::vec3(camFront.x, 0, camFront.z));
    glm::vec3 right = glm::normalize(glm::cross(flat, camUp));
    glm::vec3 dir(0);

    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) dir += flat;
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) dir -= flat;
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) dir -= right;
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) dir += right;

    if (glm::length(dir) > 0.001f) dir = glm::normalize(dir);
    player.vel.x = dir.x * speed;
    player.vel.z = dir.z * speed;

    if (glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS && player.onGround && !player.crouching) {
        player.vel.y = JUMP_FORCE; player.onGround = false;
    }
}

inline void mouse_callback(GLFWwindow*, double xIn, double yIn)
{
    float x = (float)xIn, y = (float)yIn;
    if (firstMouse) { lastX = x; lastY = y; firstMouse = false; }
    yaw += (x - lastX) * MOUSE_SENS;
    pitch += (lastY - y) * MOUSE_SENS;
    lastX = x; lastY = y;
    pitch = glm::clamp(pitch, -PITCH_LIM, PITCH_LIM);
    glm::vec3 f;
    f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    f.y = sin(glm::radians(pitch));
    f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    camFront = glm::normalize(f);
}

// forward declaration — определяется в main.cpp
struct Renderer;
extern Renderer* gRenderer;
void onWeaponSwitchRenderer(); // реализация в main.cpp

inline void key_callback(GLFWwindow* w, int key, int, int action, int)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(w, GLFW_TRUE);

    if (key == GLFW_KEY_R && action == GLFW_PRESS)
        doReload(weaponManager.activeDef().maxAmmo);

    // Переключение оружия 1/2/3/4
    if (action == GLFW_PRESS && !gun.reloading) {
        int idx = -1;
        if (key == GLFW_KEY_1) idx = 0;
        if (key == GLFW_KEY_2) idx = 1;
        if (key == GLFW_KEY_3) idx = 2;
        if (key == GLFW_KEY_4) idx = 3;

        if (idx >= 0 && idx < (int)weaponManager.models.size() && idx != weaponManager.current) {
            weaponManager.switchTo(idx);
            onWeaponSwitchRenderer();
        }
    }
}

inline void mouse_button_callback(GLFWwindow*, int button, int action, int)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        glm::vec3 camPos = player.pos + glm::vec3(0, player.eyeH, 0);
        const WeaponDef& def = weaponManager.activeDef();
        doShoot(camPos, camFront, def.fireRate, def.recoilKick);
    }
}

inline void framebuffer_size_callback(GLFWwindow*, int w, int h)
{
    glViewport(0, 0, w, h);
}