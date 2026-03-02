#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>

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

// ──────────────────────────────────────────────
//  Load placed objects from editor map file
//  and draw them in game
// ──────────────────────────────────────────────
struct GameMapObj {
    std::vector<GPUMesh> meshes;
    glm::mat4            matrix;
};

std::vector<GameMapObj> gameLevelObjects;

void loadLevelObjects(const std::string& path, unsigned int shader)
{
    std::ifstream f(path);
    if (!f) { std::cout << "[GAME] No level file: " << path << "\n"; return; }

    gameLevelObjects.clear();
    std::string line;
    std::string curModel, curTex;
    glm::vec3 pos{ 0,0,0 }, rot{ 0,0,0 }, scl{ 1,1,1 };
    bool building = false, isSpawn = false;

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line); std::string cmd; ss >> cmd;
        if (cmd == "obj") { building = true; curModel = ""; curTex = ""; pos = rot = glm::vec3(0); scl = glm::vec3(1); isSpawn = false; }
        else if (cmd == "model" && building) { std::string p; ss >> p; if (p == "SPAWN") { isSpawn = true; } else { curModel = p; } }
        else if (cmd == "tex" && building) { std::string t; ss >> t; curTex = (t == "NONE") ? "" : t; }
        else if (cmd == "pos" && building) ss >> pos.x >> pos.y >> pos.z;
        else if (cmd == "rot" && building) ss >> rot.x >> rot.y >> rot.z;
        else if (cmd == "scl" && building) ss >> scl.x >> scl.y >> scl.z;
        else if (cmd == "end" && building) {
            if (isSpawn) {
                // Override auto-spawn with editor spawn point
                player.pos = pos + glm::vec3(0, 0.1f, 0);
                player.vel = glm::vec3(0);
                std::cout << "[GAME] Spawn point: " << pos.x << " " << pos.y << " " << pos.z << "\n";
            }
            else if (!curModel.empty()) {
                GameMapObj go;
                go.meshes = loadModel(curModel, curTex, glm::mat4(1.f), false);
                glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
                m = glm::rotate(m, glm::radians(rot.y), { 0,1,0 });
                m = glm::rotate(m, glm::radians(rot.x), { 1,0,0 });
                m = glm::rotate(m, glm::radians(rot.z), { 0,0,1 });
                m = glm::scale(m, scl);
                go.matrix = m;
                gameLevelObjects.push_back(go);
            }
            building = false;
        }
    }
    std::cout << "[GAME] Loaded " << gameLevelObjects.size() << " level objects.\n";
}

void drawLevelObjects(unsigned int shader, const glm::mat4& view, const glm::mat4& proj)
{
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3f(glGetUniformLocation(shader, "lightDir"), 0.3f, -1.f, 0.4f);
    for (auto& o : gameLevelObjects) {
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(o.matrix));
        glUniform3f(glGetUniformLocation(shader, "baseColor"), 0.78f, 0.74f, 0.68f);
        for (auto& m : o.meshes) {
            if (m.texID) {
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.texID);
                glUniform1i(glGetUniformLocation(shader, "hasTexture"), 1);
                glUniform1i(glGetUniformLocation(shader, "tex"), 0);
            }
            else {
                glUniform1i(glGetUniformLocation(shader, "hasTexture"), 0);
            }
            glBindVertexArray(m.VAO);
            glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
        }
    }
}

// ──────────────────────────────────────────────
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

// ──────────────────────────────────────────────
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "3D Engine  |  ` = Console  |  quit to exit", NULL, NULL);
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    textRenderer.init("cour.ttf", 16.f, SCR_WIDTH, SCR_HEIGHT);

    Renderer renderer;
    renderer.init();
    gRenderer = &renderer;

    console.init();

    // Load terrain
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    std::cout << "[GAME] Loading terrain...\n";
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    std::cout << "[GAME] Building BVH...\n";
    bvh.build(colTris);

    // Auto-spawn then override with editor spawn if available
    autoSpawn();

    // Load editor-placed objects
    loadLevelObjects("maps/level.map", renderer.worldShader);

    std::cout << "[GAME] Loading weapons...\n";
    weaponManager.loadAll();
    gun.ammo = weaponManager.activeDef().maxAmmo;

    std::cout << "[GAME] Loading enemies...\n";
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
            std::cout << "[GAME] Dead! Respawning.\n";
        }

        glClearColor(0.4f, 0.6f, 0.9f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
        glm::mat4 proj = glm::perspective(glm::radians(FOV),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);

        // 3D
        glEnable(GL_DEPTH_TEST);
        renderer.drawScene(mapMeshes, weaponManager.active(), mapT, camPos, camFront, camUp);

        // Draw editor-placed objects
        drawLevelObjects(renderer.worldShader, view, proj);

        enemyManager.draw(renderer.worldShader, view, proj);

        // 2D HUD + console
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
            textRenderer.drawText(enStr, 8.f, (float)SCR_HEIGHT - 40.f, { 0.85f,0.85f,0.85f,1.f });
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