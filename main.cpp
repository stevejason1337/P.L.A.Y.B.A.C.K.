#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Settings.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "Player.h"
#include "WeaponManager.h"
#include "TextRenderer.h"
#include "Console.h"
#include "Renderer.h"
#include "Input.h"

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
        "FPS | WASD  Shift Sprint  Space Jump  Ctrl Crouch  LMB Shoot  R Reload  1/2 Weapon  ~ Console  ESC Quit",
        NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCharCallback(window, char_callback);  // ← для ввода текста в консоль
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // TextRenderer — инициализируем до консоли
    textRenderer.init("C:/123321/Project1/x64/Debug/cour.ttf", 16.f, SCR_WIDTH, SCR_HEIGHT);

    Renderer renderer;
    renderer.init();
    gRenderer = &renderer;

    // Console
    console.init();

    // Map
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    std::cout << "[INFO] Loading map...\n";
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    std::cout << "[INFO] Building BVH...\n";
    bvh.build(colTris);
    autoSpawn();

    // Weapons
    std::cout << "[INFO] Loading weapons...\n";
    weaponManager.loadAll();
    gun.ammo = weaponManager.activeDef().maxAmmo;

    float dt = 0.f, last = 0.f;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        dt = (now - last) * timeScale; last = now;
        if (dt > 0.1f) dt = 0.1f;

        processMovement(window);

        // Noclip — отключаем гравитацию
        if (!noclip) {
            updatePlayer(dt);
        }
        else {
            // В noclip просто двигаемся без физики
            player.pos += player.vel * dt;
            player.onGround = false;
        }

        updateGun(dt);
        console.update(dt);
        renderer.updateGunAnim(weaponManager.active(), dt);

        glClearColor(0.4f, 0.6f, 0.9f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 camPos = player.pos + glm::vec3(0, player.eyeH, 0);
        renderer.drawScene(mapMeshes, weaponManager.active(), mapT, camPos, camFront, camUp);

        // HUD и консоль (2D поверх)
        glDisable(GL_DEPTH_TEST);
        console.drawHUD(now - last < 0.0001f ? dt : now - last);
        console.draw();
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}