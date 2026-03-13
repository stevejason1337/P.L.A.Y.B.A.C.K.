// Правильный порядок: windows.h → glad → glfw3 → glfw3native → остальное
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <glad/glad.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

#include <vector>
#include "Settings.h"       // loadEngineConfig / saveEngineConfig / gRenderBackend
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
#include "BulletIntegration.h"
#include "BloodFX.h"

// --- Dear ImGui -----------------------------------------------
#include <imgui.h>
#include <imgui_impl_glfw.h>
#ifdef _WIN32
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#endif
#include <imgui_impl_opengl3.h>

#include "MainMenu.h"        // Главное меню + экран загрузки

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
bool gReturnToMenu = false;
MenuState gGetMenuState() { return gMenu.state; }
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
// -------------------------------------------------------------

// -------------------------------------------------------------
//  Меню выбора API - показывается если engine.cfg не найден
//  или пользователь нажал F10 в игре
// -------------------------------------------------------------
static void showAPISelectMenu()
{
#ifdef _WIN32
    // На Windows показываем MessageBox как в старых играх
    int result = MessageBoxA(
        nullptr,
        "Choose Graphics API:\n\n"
        "[Yes]  = DirectX 11  (рекомендуется на Windows)\n"
        "[No]   = OpenGL 3.3  (кроссплатформенный)\n\n"
        "Выбор сохранится в engine.cfg",
        "Graphics API",
        MB_YESNO | MB_ICONQUESTION
    );
    gRenderBackend = (result == IDYES) ? RenderBackend::DX11 : RenderBackend::OpenGL;
    saveEngineConfig();
#else
    // На Linux/Mac - только OpenGL
    gRenderBackend = RenderBackend::OpenGL;
#endif
}

int main()
{
    // -- 1. Читаем сохранённый выбор API ----------------------
    bool cfgExists = false;
    {
        std::ifstream f(ENGINE_CFG);
        cfgExists = f.is_open();
    }
    if (cfgExists) {
        loadEngineConfig();
    }
    else {
        // Первый запуск - спрашиваем
        showAPISelectMenu();
    }

    // -- 2. GLFW init ------------------------------------------
    if (!glfwInit()) return -1;

    // Для DX11 нам не нужен OpenGL контекст - указываем GLFW не создавать его
#ifdef _WIN32
    if (gRenderBackend == RenderBackend::DX11) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else
#endif
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "P.L.A.Y.B.A.C.K.", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

#ifdef _WIN32
    if (gRenderBackend == RenderBackend::OpenGL)
#endif
    {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            fprintf(stderr, "[MAIN] Failed to load OpenGL\n");
            glfwTerminate(); return -1;
        }
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // -- 3. Получаем нативный хэндл окна ----------------------
    void* nativeWindow = nullptr;
#ifdef _WIN32
    nativeWindow = (void*)glfwGetWin32Window(window);
#endif

    // -- 4. Колбэки -------------------------------------------
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // -- 5. Console init ------------------------------------------
    console.init();

    // ModelLoader: на OpenGL грузит текстуры в GL сразу,
    // на DX11 — сохраняет пиксели в CPU для последующего uploadMeshes()
    gLoadGLTextures = (gRenderBackend == RenderBackend::OpenGL);

    // -- 6. Renderer - главная инициализация ------------------
    renderer.init(nativeWindow);
    printf("[MAIN] Backend active: %s\n", renderer.backendName());

    // Подключаем консольные колбэки (разрываем circular dependency)
    gSetWireframe = [](bool on) { renderer.setWireframe(on); };
    gGetBackendName = []() -> const char* { return renderer.backendName(); };

    // -- ImGui init --------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& imio = ImGui::GetIO();
    imio.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imio.IniFilename = nullptr; // не сохранять imgui.ini

    // Стиль - тёмный, чуть настроен под игру
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.f;
    style.FrameRounding = 3.f;
    style.ScrollbarRounding = 3.f;
    style.GrabRounding = 3.f;
    style.Alpha = 1.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.90f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.14f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.18f, 0.12f, 1.00f);

#ifdef _WIN32
    if (gRenderBackend == RenderBackend::DX11) {
        ImGui_ImplGlfw_InitForOther(window, true);
        ImGui_ImplDX11_Init(
            static_cast<ID3D11Device*>(renderer.getDX11Device()),
            static_cast<ID3D11DeviceContext*>(renderer.getDX11Context()));
    }
    else
#endif
    {
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    // -- 7. Если DX11 упал - переключаемся на OpenGL ----------
#ifdef _WIN32
    if (gRenderBackend == RenderBackend::DX11 && !renderer.isDX11Ready()) {
        printf("[MAIN] DX11 init failed, switching to OpenGL\n");
        gRenderBackend = RenderBackend::OpenGL;
        saveEngineConfig();
        // Пересоздаём окно с GL контекстом
        glfwTerminate();
        if (!glfwInit()) return -1;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "P.L.A.Y.B.A.C.K.", NULL, NULL);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glfwSetCursorPosCallback(window, mouse_callback);
        glfwSetMouseButtonCallback(window, mouse_button_callback);
        glfwSetKeyCallback(window, key_callback);
        glfwSetCharCallback(window, char_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        renderer.init(nullptr);
    }
#endif

    // -- 8. Карта ---------------------------------------------
    glm::mat4 mapT = glm::rotate(
        glm::scale(glm::mat4(1.f), glm::vec3(MAP_SCALE)),
        glm::radians(MAP_ROT_X), glm::vec3(1, 0, 0));

    printf("[MAIN] Loading map...\n");
    auto mapMeshes = loadModel(MAP_FILE, MAP_TEX_DIR, mapT, true);

    // Для DX11 - загружаем меши в GPU
    renderer.uploadMeshes(mapMeshes);

    printf("[MAIN] Building BVH...\n");
    bvh.build(colTris);

    // Bullet Physics + Blood FX
    bulletWorld.init();
    bulletWorld.addMapCollision();
    // BloodFX: передаём device/context — на OpenGL они nullptr, функция игнорирует
    bloodFX.init(renderer.getDX11Device(), renderer.getDX11Context());

    // -- 9. Оружие, враги, звук -------------------------------
    weaponManager.loadAll();
    gun.ammo = weaponManager.activeDef().maxAmmo;

    renderer.uploadModels(weaponManager.models);

    enemyManager.load();
    // Подключаем выстрел игрока к enemyManager (обходим circular include)
    gShootEnemyFn = [](const glm::vec3& o, const glm::vec3& d, float* distOut) -> int {
        return enemyManager.rayHit(o, d, 200.f, distOut, d);
        };
    renderer.uploadMeshes(gModelSoldier.proto.meshes);
    renderer.uploadMeshes(gModelZombie.proto.meshes);
    renderer.uploadMeshes(gModelZombie2.proto.meshes);
    // Load enemy spawns from map file (only if no enemies already present)
    {
        std::ifstream mf("maps/level.map");
        if (mf) {
            std::string line, cmd;
            bool inObj = false;
            std::string eType; glm::vec3 ePos(0);
            bool isEnemy = false;
            std::vector<std::pair<std::string, glm::vec3>> pendingSpawns;
            while (std::getline(mf, line)) {
                std::istringstream ss(line);
                ss >> cmd;
                if (cmd == "obj") { inObj = true; eType = ""; ePos = glm::vec3(0); isEnemy = false; }
                else if (cmd == "pos" && inObj) { ss >> ePos.x >> ePos.y >> ePos.z; }
                else if (cmd == "enemy" && inObj) { isEnemy = true; ss >> eType; }
                else if (cmd == "end" && inObj) {
                    if (isEnemy) pendingSpawns.push_back({ eType, ePos });
                    inObj = false;
                }
            }
            // Deduplicate by position (remove entries closer than 0.5 units)
            std::vector<std::pair<std::string, glm::vec3>> uniqueSpawns;
            for (auto& sp : pendingSpawns) {
                bool dup = false;
                for (auto& us : uniqueSpawns)
                    if (glm::distance(us.second, sp.second) < 0.5f) { dup = true; break; }
                if (!dup) uniqueSpawns.push_back(sp);
            }
            // Only spawn from map if enemies list is currently empty
            if (enemyManager.enemies.empty()) {
                for (auto& [t, p] : uniqueSpawns) {
                    if (t == "zombie")       enemyManager.spawnZombie(p);
                    else if (t == "zombie2") enemyManager.spawnZombie2(p);
                    else if (t == "soldier") enemyManager.spawn(p);
                    std::cout << "[MAP] Spawned " << t << " at " << p.x << "," << p.y << "," << p.z << "\n";
                }
            }
        }
    }
    soundManager.init();

    // -- 10. Стартовая позиция игрока -------------------------
    {
        float gy = getGroundY(player.pos, 200.f);
        if (gy != std::numeric_limits<float>::lowest())
            player.pos.y = gy + player.eyeH;
    }

    float lastFrame = 0.f;

    // -- Инициализация главного меню ---------------------------
    gMenu.init();
    // Колбэк применения настроек окна
    gMenu.onApplySettings = [&](int w, int h, WindowMode wm, RenderBackend) {
        if (wm == WindowMode::FULLSCREEN) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            glfwSetWindowMonitor(window, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        }
        else if (wm == WindowMode::BORDERLESS) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            glfwSetWindowMonitor(window, nullptr, 0, 0, vm->width, vm->height, 0);
        }
        else {
            glfwSetWindowMonitor(window, nullptr, 100, 100, w, h, 0);
        }
        glfwPollEvents();
        int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH);
        renderer.resize(fbW > 0 ? fbW : w, fbH > 0 ? fbH : h);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };

    // ════════════════════════════════════════════════════════
    //  ГЛАВНЫЙ ЦИКЛ
    // ════════════════════════════════════════════════════════
    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        float rawDt = now - lastFrame;
        lastFrame = now;
        float dt = rawDt * timeScale;
        if (dt > 0.1f) dt = 0.1f;

        // Счётчик FPS
        static float fpsAccum = 0.f; static int fpsCnt = 0;
        fpsAccum += rawDt; fpsCnt++;
        if (fpsAccum >= 0.25f) { fpsValue = (float)fpsCnt / fpsAccum; fpsAccum = 0.f; fpsCnt = 0; }

        // -- Ввод --
        glfwPollEvents();

        // -- Главное меню -------------------------------------
        if (gMenu.state != MenuState::INGAME) {
            // Курсор свободный в меню
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

            if (gRenderBackend == RenderBackend::OpenGL) {
                glClearColor(0.04f, 0.05f, 0.06f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }
            renderer.beginFrame();
            gMenu.draw(window, dt);
            renderer.endFrame();
            renderer.swapBuffers(window);
            continue;
        }
        // Захватываем курсор при входе в игру
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // disconnect command - return to main menu
        if (gReturnToMenu) {
            gReturnToMenu = false;
            gMenu.state = MenuState::MAIN;
            gMenu.fadingIn = true;
            gMenu.fadeAlpha = 1.f;
            continue;
        }

        processMovement(window);

        // -- Обновление --
        updatePlayer(dt);
        soundManager.playFootstep(dt, playerMoving, player.onGround);
        console.update(dt);

        glm::vec3 camPos = player.pos + glm::vec3(0.f, player.eyeH, 0.f);
        enemyManager.update(dt, camPos, playerHP);

        bulletWorld.update(dt);
        bloodFX.update(dt);

        renderer.updateGunAnim(weaponManager.active(), dt);

        // -- Рендер --
        renderer.renderShadowPass(mapMeshes, camPos);
        renderer.beginFrame();

        // glClear только для OpenGL - DX11 очищает в beginFrame()
        if (gRenderBackend == RenderBackend::OpenGL) {
            glClearColor(0.68f, 0.65f, 0.60f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        renderer.drawScene(mapMeshes, weaponManager.active(), mapT, camPos, camFront, camUp);

        // Враги
        {
            glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
            float curFOV = glm::mix(FOV, FOV * 0.6f, gun.adsProgress);
            glm::mat4 proj = glm::perspective(
                glm::radians(curFOV),
                (float)SCR_WIDTH / (float)SCR_HEIGHT,
                0.05f, 5000.f);
            for (auto& e : enemyManager.enemies) {
                if (e.removed) continue;
                renderer.drawEnemy(e.getMatrix(), e.boneFinal,
                    e.sharedModel().proto.meshes, view, proj);
            }

            bloodFX.draw(view, proj);
        }

        renderer.imguiNewFrame();
        gHitMarker.draw();

        // HUD (работает на обоих API через ImGui DrawList)
        if (showHUD)
            drawHUD(rawDt, fpsValue, showFPS, showPos, noclip, godMode,
                renderer.reloadTimer, renderer.reloadDuration);
        console.draw();

        // Смерть
        if (playerHP <= 0.f && !godMode) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 dsz = ImGui::GetIO().DisplaySize;
            // Красный оверлей
            dl->AddRectFilled({ 0,0 }, dsz, IM_COL32(130, 0, 0, 115));
            // "YOU DIED"
            ImGui::SetNextWindowPos({ dsz.x * 0.5f, dsz.y * 0.5f },
                ImGuiCond_Always, { 0.5f,0.5f });
            ImGui::SetNextWindowBgAlpha(0.f);
            ImGui::Begin("##died", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SetWindowFontScale(2.8f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.08f, 0.08f, 1.f));
            ImGui::Text("YOU DIED");
            ImGui::PopStyleColor();
            ImGui::End();

            static float deathTimer = 0.f;
            deathTimer += rawDt;
            if (deathTimer > 3.f) {
                deathTimer = 0.f;
                playerHP = playerMaxHP;
                player.pos = glm::vec3(0.f, 20.f, 0.f);
                player.vel = glm::vec3(0.f);
            }
        }

        renderer.imguiRender();

        renderer.postHp01 = std::max(0.f, std::min(1.f, playerHP / playerMaxHP));
        renderer.endFrame(dt);
        renderer.swapBuffers(window);
    }

    soundManager.shutdown();
    bulletWorld.shutdown();
    bloodFX.shutdown();
    renderer.imguiShutdown();

    glfwTerminate();
    return 0;
}