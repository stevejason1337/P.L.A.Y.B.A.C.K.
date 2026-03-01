#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Settings.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "Player.h"
#include "Enemy.h"
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

void shootWithEnemyCheck(const glm::vec3& camPos, const glm::vec3& camFront,
    float fireRate, float recoilKick)
{
    doShoot(camPos, camFront, fireRate, recoilKick);
    int hit = enemyManager.rayHit(camPos, camFront);
    if (hit >= 0)
        enemyManager.enemies[hit].takeDamage(1.f);
    else
        doShootWorld(camPos, camFront);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "3D Engine", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Включаем blend один раз глобально
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // TextRenderer ПЕРВЫМ — до console.init()
    textRenderer.init("cour.ttf", 16.f, SCR_WIDTH, SCR_HEIGHT);

    Renderer renderer;
    renderer.init();
    gRenderer = &renderer;

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

    std::cout << "[INFO] Loading weapons...\n";
    weaponManager.loadAll();
    gun.ammo = weaponManager.activeDef().maxAmmo;

    std::cout << "[INFO] Loading enemies...\n";
    enemyManager.load();
    enemyManager.spawnGroup(player.pos + glm::vec3(5, 0, 5), 3, 4.f);

    float dt = 0.f, last = 0.f;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        dt = (now - last) * timeScale; last = now;
        if (dt > 0.1f) dt = 0.1f;

        glm::vec3 camPos = player.pos + glm::vec3(0, player.eyeH, 0);

        processMovement(window);

        if (!noclip) updatePlayer(dt);
        else { player.pos += player.vel * dt; player.onGround = false; }

        updateGun(dt);
        console.update(dt);
        renderer.updateGunAnim(weaponManager.active(), dt);
        enemyManager.update(dt, camPos, playerHP);

        if (playerHP <= 0.f && !godMode) {
            playerHP = playerMaxHP;
            autoSpawn();
            std::cout << "[PLAYER] Dead! Respawning.\n";
        }

        glClearColor(0.4f, 0.6f, 0.9f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
        glm::mat4 proj = glm::perspective(glm::radians(FOV),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);

        // 3D
        glEnable(GL_DEPTH_TEST);
        renderer.drawScene(mapMeshes, weaponManager.active(), mapT, camPos, camFront, camUp);
        enemyManager.draw(renderer.worldShader, view, proj);

        // 2D HUD + консоль
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        console.drawHUD(dt);

        if (showHUD) {
            std::string hpStr = "HP: " + std::to_string((int)playerHP);
            glm::vec4 hpCol = playerHP > 60 ? glm::vec4(0.4f, 1.f, 0.4f, 1.f)
                : playerHP > 30 ? glm::vec4(1.f, 0.8f, 0.2f, 1.f)
                : glm::vec4(1.f, 0.3f, 0.3f, 1.f);
            textRenderer.drawText(hpStr, 8.f, (float)SCR_HEIGHT - 20.f, hpCol);

            std::string enStr = "Enemies: " + std::to_string(enemyManager.enemies.size());
            textRenderer.drawText(enStr, 8.f, (float)SCR_HEIGHT - 40.f,
                glm::vec4(0.85f, 0.85f, 0.85f, 1.f));
        }

        console.draw();

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}