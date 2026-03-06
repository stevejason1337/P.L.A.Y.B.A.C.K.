#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include "Settings.h"
#include "TextRenderer.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "AnimatedModel.h"
#include "Player.h"
#include "Soundmanager.h"
#include "WeaponManager.h"
#include "ThreadPool.h"
#include "Enemy.h"
#include "Console.h"
#include "HUD.h"
#include "Renderer.h"
#include "Input.h"

// ─── ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (определяются один раз) ───────
Character        player;
GunState         gun;
float            flashTimer = 0.f;
int              fireAnimCounter = 0;
std::vector<BulletHole> bulletHoles;
SoundManager     soundManager;
Renderer         renderer;

glm::vec3 camFront = glm::vec3(0.f, 0.f, -1.f);
glm::vec3 camUp = glm::vec3(0.f, 1.f, 0.f);
bool      playerMoving = false;
// ─────────────────────────────────────────────────────────

int main()
{
    // ── GLFW / OpenGL ──
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        SCR_WIDTH, SCR_HEIGHT, "FPS Game", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);   // vsync выключен для максимального FPS

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── Колбэки ──
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // ── Шрифт ──
    textRenderer.init("cour.ttf", 16.f, SCR_WIDTH, SCR_HEIGHT);
    console.init();

    // ── Рендерер ──
    renderer.init();

    // ── Карта ──
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    std::cout << "[MAIN] Loading map...\n";
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    std::cout << "[MAIN] Building BVH...\n";
    bvh.build(colTris);

    // ── Оружие ──
    weaponManager.loadAll();
    gun.ammo = weaponManager.activeDef().maxAmmo;

    // ── Враги ──
    enemyManager.load();

    // ── Звук ──
    soundManager.init();

    // ── Спавн стартовой позиции игрока ──
    {
        float gy = getGroundY(player.pos, 200.f);
        if (gy != std::numeric_limits<float>::lowest())
            player.pos.y = gy + player.eyeH;
    }

    float lastFrame = 0.f;

    // ════════════════════════════════════════
    //  ГЛАВНЫЙ ЦИКЛ
    // ════════════════════════════════════════
    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        float rawDt = now - lastFrame;
        lastFrame = now;
        float dt = rawDt * timeScale;
        if (dt > 0.1f) dt = 0.1f;

        // ── Ввод ──
        glfwPollEvents();
        processMovement(window);

        // ── Обновление ──
        updatePlayer(dt);
        soundManager.playFootstep(dt, playerMoving, player.onGround);
        console.update(dt);

        glm::vec3 camPos = player.pos + glm::vec3(0.f, player.eyeH, 0.f);
        enemyManager.update(dt, camPos, playerHP);

        // Обновление анимации оружия
        renderer.updateGunAnim(weaponManager.active(), dt);

        // ── Очистка ──
        renderer.beginFrame();
        glClearColor(0.68f, 0.65f, 0.60f, 1.f);   // тёплое небо = цвет тумана
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ── Сцена ──
        renderer.drawScene(
            mapMeshes,
            weaponManager.active(),
            mapT,
            camPos, camFront, camUp);

        // ── Враги — gunShader для skinning, proj без ADS zoom ──
        {
            glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
            // Используем тот же adsFOV что и мир, чтобы враги не плыли
            float curFOV = glm::mix(FOV, FOV * 0.6f, gun.adsProgress);
            glm::mat4 proj = glm::perspective(
                glm::radians(curFOV),
                (float)SCR_WIDTH / (float)SCR_HEIGHT,
                0.05f, 5000.f);
            enemyManager.draw(renderer.gunShader, view, proj);
        }

        // ── HUD / Консоль ──
        drawHUD(rawDt, fpsValue, showFPS, showPos, noclip, godMode,
            renderer.reloadTimer, renderer.reloadDuration);
        console.draw();

        // ── Смерть ──
        if (playerHP <= 0.f && !godMode) {
            textRenderer.drawRect(0, 0, (float)SCR_WIDTH, (float)SCR_HEIGHT,
                glm::vec4(0.5f, 0.f, 0.f, 0.45f));
            float tw = textRenderer.textWidth("YOU DIED");
            textRenderer.drawText("YOU DIED",
                (float)SCR_WIDTH / 2.f - tw / 2.f,
                (float)SCR_HEIGHT / 2.f,
                glm::vec4(1.f, 0.f, 0.f, 1.f));

            // Автоматический респаун через 3 сек
            static float deathTimer = 0.f;
            deathTimer += rawDt;
            if (deathTimer > 3.f) {
                deathTimer = 0.f;
                playerHP = playerMaxHP;
                player.pos = glm::vec3(0.f, 20.f, 0.f);
                player.vel = glm::vec3(0.f);
            }
        }

        renderer.endFrame();
        glfwSwapBuffers(window);
    }

    soundManager.shutdown();
    glfwTerminate();
    return 0;
}