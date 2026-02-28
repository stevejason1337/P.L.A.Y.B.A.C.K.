#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Settings.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "Player.h"
#include "WeaponManager.h"
#include "Renderer.h"
#include "Input.h"

// Нужен в Input.h для onWeaponSwitch
Renderer* gRenderer = nullptr;

void onWeaponSwitchRenderer()
{
    if (gRenderer) gRenderer->onWeaponSwitch();
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "FPS | WASD  Shift Sprint  Space Jump  Ctrl Crouch  LMB Shoot  R Reload  1/2 Weapon  ESC Quit",
        NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    Renderer renderer;
    renderer.init();
    gRenderer = &renderer;

    // Map
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    std::cout << "[INFO] Loading map...\n";
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    std::cout << "[INFO] Building BVH...\n";
    bvh.build(colTris);
    autoSpawn();

    // Загружаем все оружия
    std::cout << "[INFO] Loading weapons...\n";
    weaponManager.loadAll();
    gun.ammo = weaponManager.activeDef().maxAmmo;

    float dt = 0.f, last = 0.f;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        dt = now - last; last = now;
        if (dt > 0.05f) dt = 0.05f;

        processMovement(window);
        updatePlayer(dt);
        updateGun(dt);
        renderer.updateGunAnim(weaponManager.active(), dt);

        glClearColor(0.4f, 0.6f, 0.9f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 camPos = player.pos + glm::vec3(0, player.eyeH, 0);
        renderer.drawScene(mapMeshes, weaponManager.active(), mapT, camPos, camFront, camUp);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}