#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Settings.h"
#include "ModelLoader.h"   // includes AnimatedModel.h, defines Triangle, texCache
#include "AABB.h"
#include "Player.h"
#include "Renderer.h"
#include "Input.h"

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "FPS | WASD  Shift Sprint  Space Jump  Ctrl Crouch  LMB Shoot  R Reload  ESC Quit",
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

    // Map
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    std::cout << "[INFO] Loading map...\n";
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    std::cout << "[INFO] Building BVH...\n";
    bvh.build(colTris);
    autoSpawn();

    // Gun
    std::cout << "[INFO] Loading gun...\n";
    AnimatedModel gunModel;
    gunModel.texLoader = loadTexture; // передаём функцию загрузки текстур
    gunModel.load(GUN_FILE, GUN_TEX_DIR);
    if (gunModel.hasAnim(ANIM_IDLE)) gunModel.play(ANIM_IDLE, true);

    float dt = 0.f, last = 0.f;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        dt = now - last; last = now;
        if (dt > 0.05f) dt = 0.05f;

        processMovement(window);
        updatePlayer(dt);
        updateGun(dt);
        renderer.updateGunAnim(gunModel, dt);

        glClearColor(0.4f, 0.6f, 0.9f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 camPos = player.pos + glm::vec3(0, player.eyeH, 0);
        renderer.drawScene(mapMeshes, gunModel, mapT, camPos, camFront, camUp);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}