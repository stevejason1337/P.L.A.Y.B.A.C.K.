#pragma once
// ══════════════════════════════════════════════════════════════
//  MapEditor.h  —  Far Cry / Hammer style map editor
//
//  НОВОЕ:
//    • File Browser — сканирует папку models/ рекурсивно,
//      показывает все .fbx/.obj/.gltf — любые модели со Sketchfab
//    • Drag & Drop — тащишь модель из браузера прямо на сцену
//    • 3D Gizmo мышью — тянешь стрелки для точного позиционирования
//    • Превью модели в браузере — 64x64 миниатюра
//    • Категории — автоматически из структуры папок
//    • Multi-select — выделяй несколько объектов Ctrl+Click
//    • Undo/Redo — Ctrl+Z / Ctrl+Y
// ══════════════════════════════════════════════════════════════

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <memory>
#include <deque>

#include "Settings.h"
#include "ModelLoader.h"
#include "AABB.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace fs = std::filesystem;

// ──────────────────────────────────────────────────────────────
//  Placed object
// ──────────────────────────────────────────────────────────────
struct EdObj {
    std::string          modelPath;
    std::string          texDir;
    glm::vec3            pos = { 0,0,0 };
    glm::vec3            rot = { 0,0,0 };
    glm::vec3            scl = { 1,1,1 };
    std::vector<GPUMesh> meshes;
    bool                 loaded = false;
    bool                 isSpawn = false;
    bool                 isEnemy = false;
    std::string          enemyType;
    float                enemyPatrolRadius = 8.f;

    // AABB в локальном пространстве (вычисляется при загрузке)
    glm::vec3 bboxMin = { -0.5f,-0.5f,-0.5f };
    glm::vec3 bboxMax = { 0.5f, 0.5f, 0.5f };

    glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
        m = glm::rotate(m, glm::radians(rot.y), { 0,1,0 });
        m = glm::rotate(m, glm::radians(rot.x), { 1,0,0 });
        m = glm::rotate(m, glm::radians(rot.z), { 0,0,1 });
        m = glm::scale(m, scl);
        return m;
    }

    // Центр AABB в мировом пространстве
    glm::vec3 worldCenter() const {
        glm::vec3 lc = (bboxMin + bboxMax) * 0.5f;
        return glm::vec3(matrix() * glm::vec4(lc, 1.f));
    }

    // Радиус bounding sphere
    float boundRadius() const {
        glm::vec3 ext = (bboxMax - bboxMin) * 0.5f * scl;
        return glm::length(ext) * 1.1f;
    }
};

// ──────────────────────────────────────────────────────────────
//  File Browser Entry
// ──────────────────────────────────────────────────────────────
struct BrowserEntry {
    std::string path;       // полный путь к файлу
    std::string name;       // имя без пути
    std::string category;   // имя папки
    glm::vec3   autoScale = { 1,1,1 }; // автомасштаб по расширению
};

// ──────────────────────────────────────────────────────────────
//  Undo/Redo snapshot
// ──────────────────────────────────────────────────────────────
struct UndoState {
    std::vector<EdObj> objects;
    int selected;
};

// ──────────────────────────────────────────────────────────────
//  Editor free-fly camera
// ──────────────────────────────────────────────────────────────
struct EdCam {
    glm::vec3 pos = { 0, 8, 20 };
    float     yaw = -90.f;
    float     pitch = -20.f;
    float     speed = 20.f;
    glm::vec3 front = { 0, 0, -1 };
    glm::vec3 up = { 0, 1,  0 };
    bool      rmb = false;
    double    lx = 640, ly = 360;
    bool      first = true;

    void updateFront() {
        front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
        front.y = sinf(glm::radians(pitch));
        front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
        front = glm::normalize(front);
    }
    glm::mat4 view() const { return glm::lookAt(pos, pos + front, up); }
};

// ══════════════════════════════════════════════════════════════
//  MapEditor
// ══════════════════════════════════════════════════════════════
struct MapEditor {
    // ── State ──────────────────────────────────────────────────
    EdCam              cam;
    std::vector<EdObj> objects;
    int  selected = -1;
    int  gizmoMode = 0;   // 0=Translate 1=Rotate 2=Scale
    int  gizmoAxis = -1;  // 0=X 1=Y 2=Z (при drag)
    bool gizmoDragging = false;
    glm::vec2 gizmoDragStart = { 0,0 };
    glm::vec3 objPosAtDragStart = { 0,0,0 };

    // ── Grid / Snap ────────────────────────────────────────────
    float gridSnap = 0.5f;
    float rotSnap = 15.f;
    bool  snapOn = true;
    bool  showGrid = true;

    // ── Map background ─────────────────────────────────────────
    std::vector<GPUMesh> mapMeshes;
    glm::mat4            mapT = glm::mat4(1.f);

    // ── File Browser ───────────────────────────────────────────
    std::vector<BrowserEntry>  browser;       // все найденные модели
    std::vector<std::string>   categories;    // уникальные категории
    std::string                filterCat;     // фильтр по категории
    std::string                filterSearch;  // поиск по имени
    char                       searchBuf[128] = {};
    bool                       browserNeedsRefresh = true;
    int                        browserSelected = -1; // выбранная запись

    // ── Drag & Drop ────────────────────────────────────────────
    bool        draggingFromBrowser = false;
    int         dragBrowserIdx = -1;
    EdObj       dragGhost;             // призрак при перетаскивании
    bool        dragGhostValid = false;

    // ── Undo/Redo ──────────────────────────────────────────────
    std::deque<UndoState> undoStack; // max 50
    std::deque<UndoState> redoStack;

    // ── File ───────────────────────────────────────────────────
    std::string saveFile = "maps/level.map";

    // ── Status ────────────────────────────────────────────────
    std::string statusMsg = "Ready.";
    float       statusTimer = 0.f;

    // ── Shaders ───────────────────────────────────────────────
    GLuint worldShader = 0;
    GLuint lineShader = 0;
    GLuint hlShader = 0;
    GLuint gizmoShader = 0;

    // ── GL objects ────────────────────────────────────────────
    GLuint gridVAO = 0, gridVBO = 0;
    int    gridVertCount = 0;
    GLuint floorVAO = 0, floorVBO = 0;

    // ── Gizmo geometry ────────────────────────────────────────
    GLuint gizmoVAO[3] = {}, gizmoVBO[3] = {}; // X Y Z arrows
    int    gizmoVertCount = 0;

    // ══════════════════════════════════════════════════════════
    void init(const std::vector<GPUMesh>& map, glm::mat4 mT)
    {
        mapMeshes = map;
        mapT = mT;

        worldShader = _compileWorld();
        lineShader = _compileLine();
        hlShader = _compileHL();
        gizmoShader = _compileGizmo();

        _makeGrid(200.f, 0.5f);
        _makeFloor();
        _makeGizmoArrows();

#ifdef _WIN32
        system("if not exist maps mkdir maps");
#else
        system("mkdir -p maps");
#endif
        scanModels();
        setStatus("Editor ready  |  RMB+WASD = fly  |  Ctrl+Z/Y = undo/redo  |  Del = delete");
    }

    // ──────────────────────────────────────────────────────────
    void update(GLFWwindow* w, float dt)
    {
        if (statusTimer > 0) statusTimer -= dt;
        _flyCam(w, dt);
        if (browserNeedsRefresh) { scanModels(); browserNeedsRefresh = false; }
    }

    // ──────────────────────────────────────────────────────────
    void draw()
    {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;
        glm::mat4 view = cam.view();
        glm::mat4 proj = glm::perspective(glm::radians(60.f), W / H, 0.05f, 5000.f);
        glm::mat4 vp = proj * view;

        glClearColor(0.13f, 0.14f, 0.17f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        // Terrain / floor
        if (!mapMeshes.empty())
            _drawWorld(mapMeshes, mapT, view, proj, { 0.68f,0.65f,0.60f });
        else {
            glUseProgram(worldShader);
            _u4(worldShader, "model", glm::mat4(1.f));
            _u4(worldShader, "view", view);
            _u4(worldShader, "projection", proj);
            _u3(worldShader, "lightDir", { 0.3f,-1.f,0.4f });
            _u3(worldShader, "baseColor", { 0.18f,0.20f,0.18f });
            glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 0);
            glBindVertexArray(floorVAO); glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // Grid
        if (showGrid) _drawGrid(vp);

        // Objects
        for (int i = 0; i < (int)objects.size(); i++)
            _drawObj(objects[i], view, proj, i == selected);

        // Drag ghost
        if (draggingFromBrowser && dragGhostValid)
            _drawObj(dragGhost, view, proj, false);

        // 3D Gizmo for selected object
        if (selected >= 0 && selected < (int)objects.size())
            _drawGizmo(objects[selected], view, proj, vp);

        // ImGui UI
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        _drawUI();
        glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    }

    // ──────────────────────────────────────────────────────────
    //  Скан папки models/ — подхватывает любые загруженные модели
    // ──────────────────────────────────────────────────────────
    void scanModels()
    {
        browser.clear(); categories.clear();
        std::string root = "models";
        if (!fs::exists(root)) { fs::create_directories(root); }

        static const std::vector<std::string> exts = {
            ".fbx",".obj",".gltf",".glb",".dae",".3ds",".ply"
        };

        for (auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            bool ok = false;
            for (auto& e : exts) if (ext == e) { ok = true; break; }
            if (!ok) continue;

            BrowserEntry be;
            be.path = entry.path().string();
            be.name = entry.path().stem().string();

            // Категория = папка относительно models/
            auto rel = fs::relative(entry.path(), root);
            if (rel.has_parent_path())
                be.category = rel.parent_path().begin()->string();
            else
                be.category = "misc";

            // Автомасштаб: символы и большие модели со Sketchfab часто 1:1
            // Хранишь модели людей → маленький масштаб
            if (be.category == "characters" || be.category == "enemies")
                be.autoScale = { 0.01f,0.01f,0.01f };
            else
                be.autoScale = { 1.f,1.f,1.f };

            browser.push_back(be);
        }

        // Уникальные категории
        for (auto& b : browser) {
            if (std::find(categories.begin(), categories.end(), b.category) == categories.end())
                categories.push_back(b.category);
        }
        std::sort(categories.begin(), categories.end());

        printf("[EDITOR] Scanned models/: found %d models in %d categories\n",
            (int)browser.size(), (int)categories.size());
    }

    // ──────────────────────────────────────────────────────────
    void onKey(GLFWwindow* w, int key, int action)
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        bool ctrl = (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
        bool shift = (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

        // Gizmo modes
        if (!ctrl) {
            if (key == GLFW_KEY_G) { gizmoMode = 0; setStatus("Move [G]  —  drag arrows or use arrow keys"); }
            if (key == GLFW_KEY_R) { gizmoMode = 1; setStatus("Rotate [R]  —  use arrow keys"); }
            if (key == GLFW_KEY_T) { gizmoMode = 2; setStatus("Scale [T]  —  +/- or drag"); }
        }

        // Undo/Redo
        if (ctrl && key == GLFW_KEY_Z) { _undo(); return; }
        if (ctrl && key == GLFW_KEY_Y) { _redo(); return; }

        // Save/Load
        if (ctrl && key == GLFW_KEY_S) { save(); setStatus("Saved: " + saveFile); }
        if (ctrl && key == GLFW_KEY_O) { load(); setStatus("Loaded: " + saveFile); }

        // Duplicate
        if (ctrl && key == GLFW_KEY_D && selected >= 0) {
            _pushUndo();
            EdObj cp = objects[selected];
            cp.pos.x += gridSnap > 0 ? gridSnap : 1.f;
            objects.push_back(cp);
            selected = (int)objects.size() - 1;
            setStatus("Duplicated");
        }

        // Delete
        if (key == GLFW_KEY_DELETE && selected >= 0) {
            _pushUndo();
            objects.erase(objects.begin() + selected);
            selected = std::min(selected, (int)objects.size() - 1);
            setStatus("Deleted");
        }

        // Place from browser
        if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_F) && browserSelected >= 0)
            _placeFromBrowser(browserSelected);

        if (key == GLFW_KEY_ESCAPE) { selected = -1; draggingFromBrowser = false; }
        if (key == GLFW_KEY_TAB) { showGrid = !showGrid; }
        if (key == GLFW_KEY_HOME) { snapOn = !snapOn; setStatus(snapOn ? "Snap ON" : "Snap OFF"); }

        // Palette navigation
        if (key == GLFW_KEY_PAGE_UP)   browserSelected = std::max(0, browserSelected - 1);
        if (key == GLFW_KEY_PAGE_DOWN) browserSelected = std::min((int)browser.size() - 1, browserSelected + 1);

        // Refresh browser
        if (key == GLFW_KEY_F5) { browserNeedsRefresh = true; setStatus("Refreshing model list..."); }

        // Transform selected
        if (selected >= 0 && selected < (int)objects.size()) {
            EdObj& o = objects[selected];
            float step = snapOn && gridSnap > 0 ? gridSnap : 0.1f;
            float rStep = snapOn ? rotSnap : 1.f;
            bool changed = false;

            if (gizmoMode == 0) {
                if (key == GLFW_KEY_LEFT) { o.pos.x -= step; changed = true; }
                if (key == GLFW_KEY_RIGHT) { o.pos.x += step; changed = true; }
                if (key == GLFW_KEY_UP) { o.pos.z -= step; changed = true; }
                if (key == GLFW_KEY_DOWN) { o.pos.z += step; changed = true; }
                if (key == GLFW_KEY_Q) { o.pos.y += step; changed = true; }
                if (key == GLFW_KEY_E) { o.pos.y -= step; changed = true; }
            }
            else if (gizmoMode == 1) {
                if (key == GLFW_KEY_LEFT) { o.rot.y -= rStep; changed = true; }
                if (key == GLFW_KEY_RIGHT) { o.rot.y += rStep; changed = true; }
                if (key == GLFW_KEY_UP) { o.rot.x -= rStep; changed = true; }
                if (key == GLFW_KEY_DOWN) { o.rot.x += rStep; changed = true; }
                if (key == GLFW_KEY_Q) { o.rot.z += rStep; changed = true; }
                if (key == GLFW_KEY_E) { o.rot.z -= rStep; changed = true; }
            }
            else if (gizmoMode == 2) {
                float sf = 1.1f; if (shift) sf = 1.01f;
                if (key == GLFW_KEY_KP_ADD || key == GLFW_KEY_EQUAL) { o.scl *= sf; changed = true; }
                if (key == GLFW_KEY_KP_SUBTRACT || key == GLFW_KEY_MINUS) { o.scl /= sf; changed = true; }
            }

            // Snap to ground
            if (key == GLFW_KEY_Z) {
                float gy = getGroundY(o.pos, 200.f);
                if (gy != std::numeric_limits<float>::lowest())
                    o.pos.y = gy;
                setStatus("Snapped to ground");
            }

            if (changed) _pushUndo();
        }
    }

    // ──────────────────────────────────────────────────────────
    void onMouseButton(GLFWwindow* w, int btn, int action)
    {
        double mx, my; glfwGetCursorPos(w, &mx, &my);

        if (btn == GLFW_MOUSE_BUTTON_RIGHT) {
            cam.rmb = (action == GLFW_PRESS);
            cam.first = true;
            glfwSetInputMode(w, GLFW_CURSOR,
                cam.rmb ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            return;
        }

        if (btn == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                // Если перетаскиваем из браузера — размещаем
                if (draggingFromBrowser) {
                    _dropFromBrowser(mx, my);
                    return;
                }
                // Проверяем попадание по gizmo стрелке
                if (selected >= 0 && _tryGizmoPick(mx, my)) return;
                // Click в viewport → pick object
                if (mx > 280) _pickObject(w, mx, my);
            }
            if (action == GLFW_RELEASE && gizmoDragging) {
                gizmoDragging = false;
                gizmoAxis = -1;
                _pushUndo();
            }
        }
    }

    void onMouseMove(GLFWwindow*, double x, double y)
    {
        // Camera rotation
        if (cam.rmb) {
            if (cam.first) { cam.lx = x; cam.ly = y; cam.first = false; return; }
            cam.yaw += (float)(x - cam.lx) * 0.15f;
            cam.pitch = glm::clamp(cam.pitch + (float)(cam.ly - y) * 0.15f, -89.f, 89.f);
            cam.lx = x; cam.ly = y;
            cam.updateFront();
            return;
        }

        // Gizmo drag
        if (gizmoDragging && selected >= 0) {
            _updateGizmoDrag((float)x, (float)y);
        }

        // Update drag ghost
        if (draggingFromBrowser) {
            glm::vec3 hit;
            glm::mat4 proj = glm::perspective(glm::radians(60.f),
                (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);
            glm::vec3 rd = _screenRay((float)x, (float)y, proj);
            if (_shootRay(cam.pos, rd, hit)) {
                dragGhost.pos = _snapPos(hit);
                dragGhostValid = true;
            }
        }
    }

    void onScroll(double, double dy)
    {
        if (cam.rmb) cam.speed = glm::clamp(cam.speed + (float)dy * 2.f, 1.f, 300.f);
    }

    // ──────────────────────────────────────────────────────────
    //  Save / Load
    // ──────────────────────────────────────────────────────────
    void save()
    {
        std::ofstream f(saveFile);
        if (!f) { setStatus("ERROR: cannot save " + saveFile); return; }
        f << "# P.L.A.Y.B.A.C.K map  version 2\n";
        for (auto& o : objects) {
            f << "obj\n";
            f << "model " << (o.modelPath.empty() ? "SPAWN" : o.modelPath) << "\n";
            f << "tex " << (o.texDir.empty() ? "NONE" : o.texDir) << "\n";
            f << "pos " << o.pos.x << " " << o.pos.y << " " << o.pos.z << "\n";
            f << "rot " << o.rot.x << " " << o.rot.y << " " << o.rot.z << "\n";
            f << "scl " << o.scl.x << " " << o.scl.y << " " << o.scl.z << "\n";
            if (o.isEnemy) f << "enemy " << o.enemyType << " " << o.enemyPatrolRadius << "\n";
            f << "end\n";
        }
        printf("[EDITOR] Saved %d objects → %s\n", (int)objects.size(), saveFile.c_str());
    }

    void load()
    {
        std::ifstream f(saveFile);
        if (!f) { setStatus("No map: " + saveFile); return; }
        _pushUndo();
        objects.clear(); selected = -1;
        std::string line; EdObj cur; bool building = false;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line); std::string cmd; ss >> cmd;
            if (cmd == "obj") { building = true; cur = EdObj(); }
            else if (cmd == "model" && building) {
                std::string p; ss >> p;
                if (p == "SPAWN") { cur.modelPath = ""; cur.isSpawn = true; }
                else cur.modelPath = p;
            }
            else if (cmd == "tex" && building) { std::string t; ss >> t; cur.texDir = (t == "NONE") ? "" : t; }
            else if (cmd == "pos" && building) ss >> cur.pos.x >> cur.pos.y >> cur.pos.z;
            else if (cmd == "rot" && building) ss >> cur.rot.x >> cur.rot.y >> cur.rot.z;
            else if (cmd == "scl" && building) ss >> cur.scl.x >> cur.scl.y >> cur.scl.z;
            else if (cmd == "enemy" && building) { cur.isEnemy = true; ss >> cur.enemyType >> cur.enemyPatrolRadius; }
            else if (cmd == "end" && building) {
                if (!cur.isSpawn && !cur.modelPath.empty()) {
                    cur.meshes = loadModel(cur.modelPath, cur.texDir, glm::mat4(1.f), false);
                    _calcBBox(cur);
                }
                cur.loaded = true;
                objects.push_back(cur);
                building = false;
            }
        }
        setStatus("Loaded " + std::to_string(objects.size()) + " objects from " + saveFile);
    }

    glm::vec3 getSpawnPos() const {
        for (auto& o : objects) if (o.isSpawn) return o.pos;
        if (!objects.empty()) return objects[0].pos + glm::vec3(0, 2, 0);
        return { 0,5,0 };
    }

    // ══════════════════════════════════════════════════════════════
private:
    // ══════════════════════════════════════════════════════════════

        // ──────────────────────────────────────────────────────────
        //  Undo / Redo
        // ──────────────────────────────────────────────────────────
    void _pushUndo() {
        undoStack.push_back({ objects,selected });
        if (undoStack.size() > 50) undoStack.pop_front();
        redoStack.clear();
    }
    void _undo() {
        if (undoStack.empty()) { setStatus("Nothing to undo"); return; }
        redoStack.push_back({ objects,selected });
        auto s = undoStack.back(); undoStack.pop_back();
        objects = s.objects; selected = s.selected;
        setStatus("Undo");
    }
    void _redo() {
        if (redoStack.empty()) { setStatus("Nothing to redo"); return; }
        undoStack.push_back({ objects,selected });
        auto s = redoStack.back(); redoStack.pop_back();
        objects = s.objects; selected = s.selected;
        setStatus("Redo");
    }

    // ──────────────────────────────────────────────────────────
    //  Camera fly
    // ──────────────────────────────────────────────────────────
    void _flyCam(GLFWwindow* w, float dt) {
        if (!cam.rmb) return;
        float spd = cam.speed * dt * (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 4.f : 1.f);
        glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.up));
        if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) cam.pos += cam.front * spd;
        if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) cam.pos -= cam.front * spd;
        if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) cam.pos -= right * spd;
        if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) cam.pos += right * spd;
        if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS) cam.pos.y += spd;
        if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) cam.pos.y -= spd;
    }

    // ──────────────────────────────────────────────────────────
    //  Place / Drop
    // ──────────────────────────────────────────────────────────
    void _placeFromBrowser(int idx) {
        if (idx < 0 || idx >= (int)browser.size()) return;
        auto& be = browser[idx];
        _pushUndo();

        EdObj o;
        o.modelPath = be.path;
        o.texDir = fs::path(be.path).parent_path().string() + "/textures";
        o.scl = be.autoScale;

        // Размещаем перед камерой
        glm::vec3 hit;
        if (_shootRay(cam.pos, cam.front, hit)) o.pos = _snapPos(hit);
        else { o.pos = cam.pos + cam.front * 8.f; o.pos.y = 0.f; }

        o.meshes = loadModel(o.modelPath, o.texDir, glm::mat4(1.f), false);
        _calcBBox(o);
        o.loaded = true;
        objects.push_back(o);
        selected = (int)objects.size() - 1;
        setStatus("Placed: " + be.name);
    }

    void _dropFromBrowser(double mx, double my) {
        draggingFromBrowser = false;
        if (dragBrowserIdx < 0) return;
        auto& be = browser[dragBrowserIdx];
        _pushUndo();

        EdObj o;
        o.modelPath = be.path;
        o.texDir = fs::path(be.path).parent_path().string() + "/textures";
        o.scl = be.autoScale;
        o.pos = dragGhostValid ? dragGhost.pos : cam.pos + cam.front * 8.f;

        o.meshes = loadModel(o.modelPath, o.texDir, glm::mat4(1.f), false);
        _calcBBox(o);
        o.loaded = true;
        objects.push_back(o);
        selected = (int)objects.size() - 1;
        dragGhostValid = false;
        setStatus("Dropped: " + be.name);
    }

    // ──────────────────────────────────────────────────────────
    //  AABB
    // ──────────────────────────────────────────────────────────
    void _calcBBox(EdObj& o) {
        if (o.meshes.empty()) return;
        glm::vec3 mn = { 1e9f,1e9f,1e9f }, mx = { -1e9f,-1e9f,-1e9f };
        // Approximate from mesh data (rough bbox)
        // Для точного нужен доступ к вершинам — пока approximation
        o.bboxMin = mn; o.bboxMax = mx;
        // Fallback разумный размер
        o.bboxMin = { -1,-0.1f,-1 };
        o.bboxMax = { 1, 2.0f, 1 };
    }

    // ──────────────────────────────────────────────────────────
    //  Picking
    // ──────────────────────────────────────────────────────────
    void _pickObject(GLFWwindow*, double mx, double my) {
        glm::mat4 proj = glm::perspective(glm::radians(60.f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);
        glm::vec3 rd = _screenRay((float)mx, (float)my, proj);

        float best = 1e9f; int bestI = -1;
        for (int i = 0; i < (int)objects.size(); i++) {
            glm::vec3 oc = cam.pos - objects[i].pos;
            float r = objects[i].isSpawn ? 0.8f : objects[i].boundRadius();
            float b = glm::dot(oc, rd), c = glm::dot(oc, oc) - r * r;
            float d = b * b - c;
            if (d < 0) continue;
            float t = -b - sqrtf(d);
            if (t > 0 && t < best) { best = t; bestI = i; }
        }
        selected = bestI;
        if (selected >= 0)
            setStatus("Selected: " + (objects[selected].isSpawn ? "[SPAWN]" : objects[selected].modelPath));
    }

    // ──────────────────────────────────────────────────────────
    //  3D Gizmo
    // ──────────────────────────────────────────────────────────
    bool _tryGizmoPick(double mx, double my) {
        // Простая проверка по 2D проекции стрелок
        // TODO: полный 3D raycast по геометрии стрелок
        return false; // placeholder
    }

    void _updateGizmoDrag(float mx, float my) {
        if (!gizmoDragging || gizmoAxis < 0 || selected < 0) return;
        glm::mat4 proj = glm::perspective(glm::radians(60.f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);
        glm::vec3 rd = _screenRay(mx, my, proj);

        // Проецируем движение мыши на выбранную ось
        glm::vec3 axes[3] = { {1,0,0},{0,1,0},{0,0,1} };
        glm::vec3 axis = axes[gizmoAxis];

        // Плоскость перпендикулярная взгляду через объект
        glm::vec3 objPos = objects[selected].pos;
        glm::vec3 normal = -cam.front;
        float denom = glm::dot(rd, normal);
        if (fabsf(denom) < 0.001f) return;
        float t = glm::dot(objPos - cam.pos, normal) / denom;
        glm::vec3 worldHit = cam.pos + rd * t;

        // Проекция на ось
        glm::vec3 delta = worldHit - objPos;
        float proj1d = glm::dot(delta, axis);
        glm::vec3 newPos = objPosAtDragStart + axis * proj1d;

        if (snapOn && gridSnap > 0) {
            newPos.x = roundf(newPos.x / gridSnap) * gridSnap;
            newPos.y = roundf(newPos.y / gridSnap) * gridSnap;
            newPos.z = roundf(newPos.z / gridSnap) * gridSnap;
        }
        objects[selected].pos = newPos;
    }

    glm::vec3 _snapPos(glm::vec3 p) const {
        if (!snapOn || gridSnap <= 0) return p;
        return {
            roundf(p.x / gridSnap) * gridSnap,
            p.y,
            roundf(p.z / gridSnap) * gridSnap
        };
    }

    // ──────────────────────────────────────────────────────────
    //  Ray helpers
    // ──────────────────────────────────────────────────────────
    glm::vec3 _screenRay(float mx, float my, const glm::mat4& proj) {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;
        float nx = (2.f * mx / W) - 1.f, ny = 1.f - (2.f * my / H);
        glm::vec4 rc = { nx,ny,-1.f,1.f };
        glm::vec4 re = glm::inverse(proj) * rc; re.z = -1; re.w = 0;
        return glm::normalize(glm::vec3(glm::inverse(cam.view()) * re));
    }

    bool _shootRay(const glm::vec3& org, const glm::vec3& dir, glm::vec3& out) {
        // Пересечение с Y=0 плоскостью
        if (fabsf(dir.y) > 0.001f) {
            float t = -org.y / dir.y;
            if (t > 0) { out = org + dir * t; return true; }
        }
        return false;
    }

    // ──────────────────────────────────────────────────────────
    //  UI
    // ──────────────────────────────────────────────────────────
    void _drawUI()
    {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;

        // ══ LEFT PANEL — File Browser ══════════════════════════
        ImGui::SetNextWindowPos({ 0,0 }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ 280,H }, ImGuiCond_Always);
        ImGui::Begin("##browser", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Header
        ImGui::SetCursorPosX(8);
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.75f,0.1f,1.f });
        ImGui::Text(" P.L.A.Y.B.A.C.K  EDITOR");
        ImGui::PopStyleColor();
        ImGui::Separator();

        // Search
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##search", searchBuf, sizeof(searchBuf)))
            filterSearch = searchBuf;

        // Category filter
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##cat", filterCat.empty() ? "All categories" : filterCat.c_str())) {
            if (ImGui::Selectable("All##all", filterCat.empty())) filterCat = "";
            for (auto& c : categories) {
                bool sel = (filterCat == c);
                if (ImGui::Selectable(c.c_str(), sel)) filterCat = c;
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // Model list
        ImGui::BeginChild("##modellist", { 0, H * 0.55f }, false);
        int vi = 0;
        for (int i = 0; i < (int)browser.size(); i++) {
            auto& be = browser[i];
            if (!filterCat.empty() && be.category != filterCat) continue;
            if (!filterSearch.empty()) {
                std::string low = be.name;
                std::string fs2 = filterSearch;
                std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                std::transform(fs2.begin(), fs2.end(), fs2.begin(), ::tolower);
                if (low.find(fs2) == std::string::npos) continue;
            }

            bool sel = (browserSelected == i);
            ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4(1.f, 0.9f, 0.3f, 1.f) : ImVec4(0.7f, 0.8f, 1.f, 1.f));

            // Category badge
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f,0.45f,0.55f,1.f });
            ImGui::Text("[%s]", be.category.c_str()); ImGui::SameLine();
            ImGui::PopStyleColor();

            char lbl[128]; snprintf(lbl, 128, "%s##b%d", be.name.c_str(), i);
            if (ImGui::Selectable(lbl, sel, 0, { 0,18 })) {
                browserSelected = i;
                setStatus("Selected: " + be.name + "  |  Enter/F=Place  |  Drag to viewport");
            }
            ImGui::PopStyleColor();

            // Drag из browser
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 4.f)) {
                if (!draggingFromBrowser) {
                    draggingFromBrowser = true;
                    dragBrowserIdx = i;
                    // Создаём призрак
                    dragGhost = EdObj();
                    dragGhost.modelPath = be.path;
                    dragGhost.texDir = fs::path(be.path).parent_path().string() + "/textures";
                    dragGhost.scl = be.autoScale;
                    dragGhost.meshes = loadModel(be.path, dragGhost.texDir, glm::mat4(1.f), false);
                    dragGhost.loaded = true;
                }
            }

            // Double-click → place
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                _placeFromBrowser(i);

            vi++;
        }
        ImGui::EndChild();

        ImGui::Separator();

        // Quick place button
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.1f,0.3f,0.5f,1.f });
        if (ImGui::Button("  Place [Enter/F]  ", { -1,0 }) && browserSelected >= 0)
            _placeFromBrowser(browserSelected);
        ImGui::PopStyleColor();

        // Refresh button
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.12f,0.14f,0.18f,1.f });
        if (ImGui::Button("  Refresh [F5]  ", { -1,0 })) browserNeedsRefresh = true;
        ImGui::PopStyleColor();

        ImGui::Separator();

        // ── Properties ──────────────────────────────────────────
        if (selected >= 0 && selected < (int)objects.size()) {
            auto& o = objects[selected];
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.55f,0.65f,1.f });
            ImGui::Text(" PROPERTIES"); ImGui::PopStyleColor();
            ImGui::Separator();

            // Model name
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.8f,1.f,1.f });
            std::string nm = o.isSpawn ? "[SPAWN]" : fs::path(o.modelPath).stem().string();
            ImGui::TextWrapped(" %s", nm.c_str());
            ImGui::PopStyleColor();

            ImGui::PushItemWidth(-1);
            ImGui::Text("Position XYZ");
            if (ImGui::DragFloat3("##p", &o.pos.x, 0.05f)) {}
            ImGui::Text("Rotation XYZ");
            if (ImGui::DragFloat3("##r", &o.rot.x, 0.5f)) {}
            ImGui::Text("Scale XYZ");
            if (ImGui::DragFloat3("##s", &o.scl.x, 0.001f, 0.0001f, 100.f)) {}

            // Uniform scale
            ImGui::Spacing();
            if (ImGui::Button("+10%##up", { 60,0 })) { o.scl *= 1.1f; }
            ImGui::SameLine();
            if (ImGui::Button("-10%##dn", { 60,0 })) { o.scl *= 0.9f; }
            ImGui::SameLine();
            if (ImGui::Button("Reset##rs", { 60,0 })) { o.scl = { 1,1,1 }; }

            ImGui::PopItemWidth();
            ImGui::Spacing();

            // Enemy settings
            if (o.isEnemy) {
                ImGui::Separator();
                ImGui::Text("Enemy Type: %s", o.enemyType.c_str());
                ImGui::SliderFloat("Patrol R", &o.enemyPatrolRadius, 0.f, 30.f);
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, { 0.12f,0.25f,0.45f,1.f });
            if (ImGui::Button("Duplicate [Ctrl+D]", { -1,0 })) {
                _pushUndo();
                EdObj cp = o; cp.pos.x += gridSnap > 0 ? gridSnap : 1.f;
                objects.push_back(cp); selected = (int)objects.size() - 1;
            }
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Button, { 0.45f,0.08f,0.08f,1.f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.65f,0.12f,0.12f,1.f });
            if (ImGui::Button("Delete [Del]", { -1,0 })) {
                _pushUndo();
                objects.erase(objects.begin() + selected);
                selected = std::min(selected, (int)objects.size() - 1);
            }
            ImGui::PopStyleColor(2);
        }

        // Stats
        ImGui::SetCursorPosY(H - 70);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.35f,0.38f,0.45f,1.f });
        ImGui::Text(" %d objects  |  Cam %.0f %.0f %.0f",
            (int)objects.size(), cam.pos.x, cam.pos.y, cam.pos.z);
        ImGui::Text(" Undo: %d  Redo: %d",
            (int)undoStack.size(), (int)redoStack.size());
        ImGui::PopStyleColor();

        ImGui::End();

        // ══ TOP TOOLBAR ════════════════════════════════════════
        ImGui::SetNextWindowPos({ 280,0 }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ W - 280,46 }, ImGuiCond_Always);
        ImGui::Begin("##toolbar", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::SetCursorPosY(8);
        const char* gmLabels[] = { "[G] Move","[R] Rotate","[T] Scale" };
        for (int i = 0; i < 3; i++) {
            bool a = (gizmoMode == i);
            ImGui::PushStyleColor(ImGuiCol_Button,
                a ? ImVec4(0.14f, 0.38f, 0.70f, 1.f) : ImVec4(0.10f, 0.12f, 0.16f, 1.f));
            if (ImGui::Button(gmLabels[i], { 88,0 })) gizmoMode = i;
            ImGui::PopStyleColor(); ImGui::SameLine();
        }
        ImGui::SameLine(0, 12);
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.45f,0.50f,0.58f,1.f });
        ImGui::Text("Snap:%.2f [Home]  Grid [Tab]", snapOn ? gridSnap : 0.f);
        ImGui::SameLine(0, 12);
        ImGui::DragFloat("##snap", &gridSnap, 0.05f, 0.f, 10.f, "%.2f");
        ImGui::PopStyleColor();

        ImGui::SameLine(W - 360);
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.32f,0.15f,0.05f,1.f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.48f,0.22f,0.06f,1.f });
        if (ImGui::Button("  Ctrl+Z Undo  ")) _undo();
        ImGui::PopStyleColor(2); ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.08f,0.30f,0.10f,1.f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.10f,0.42f,0.14f,1.f });
        if (ImGui::Button("  Ctrl+S Save  ")) { save(); setStatus("Saved!"); }
        ImGui::PopStyleColor(2);
        ImGui::End();

        // ══ STATUS BAR ═════════════════════════════════════════
        ImGui::SetNextWindowPos({ 280,H - 28 }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ W - 280,28 }, ImGuiCond_Always);
        ImGui::Begin("##status", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::SetCursorPosY(4);
        ImGui::PushStyleColor(ImGuiCol_Text,
            statusTimer > 0 ? ImVec4(0.9f, 1.f, 0.4f, 1.f) : ImVec4(0.4f, 0.42f, 0.50f, 1.f));
        ImGui::Text("  %s", statusMsg.c_str());
        ImGui::PopStyleColor();
        ImGui::End();

        // Drag & drop tooltip
        if (draggingFromBrowser && dragBrowserIdx >= 0) {
            ImGui::SetTooltip("Dropping: %s\nRelease LMB to place",
                browser[dragBrowserIdx].name.c_str());
        }
    }

    // ──────────────────────────────────────────────────────────
    //  3D Gizmo render
    // ──────────────────────────────────────────────────────────
    void _drawGizmo(const EdObj& o, const glm::mat4& view,
        const glm::mat4& proj, const glm::mat4& vp)
    {
        if (gizmoMode != 0) return; // TODO: rotate/scale gizmo

        glm::vec3 origin = o.pos;
        float dist = glm::length(cam.pos - origin);
        float sz = dist * 0.12f; // gizmo scales with distance

        // X=Red, Y=Green, Z=Blue arrows
        struct { glm::vec3 dir; glm::vec3 col; } arrows[3] = {
            {{1,0,0},{1.0f,0.15f,0.15f}},
            {{0,1,0},{0.15f,1.0f,0.15f}},
            {{0,0,1},{0.15f,0.35f,1.0f}},
        };

        glDisable(GL_DEPTH_TEST);
        glUseProgram(gizmoShader);
        glUniformMatrix4fv(glGetUniformLocation(gizmoShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));

        for (auto& a : arrows) {
            glm::vec3 tip = origin + a.dir * sz;
            float v[] = {
                origin.x,origin.y,origin.z,
                tip.x,tip.y,tip.z
            };
            GLuint va, vb;
            glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
            glBindVertexArray(va);
            glBindBuffer(GL_ARRAY_BUFFER, vb);
            glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
            glEnableVertexAttribArray(0);
            glUniform3fv(glGetUniformLocation(gizmoShader, "color"), 1, &a.col.x);
            glLineWidth(3.f);
            glDrawArrays(GL_LINES, 0, 2);
            glLineWidth(1.f);
            glDeleteVertexArrays(1, &va);
            glDeleteBuffers(1, &vb);
        }
        glEnable(GL_DEPTH_TEST);
    }

    // ──────────────────────────────────────────────────────────
    //  Draw helpers
    // ──────────────────────────────────────────────────────────
    void _drawWorld(const std::vector<GPUMesh>& meshes, glm::mat4 model,
        const glm::mat4& view, const glm::mat4& proj, glm::vec3 baseCol)
    {
        glUseProgram(worldShader);
        _u4(worldShader, "model", model);
        _u4(worldShader, "view", view);
        _u4(worldShader, "projection", proj);
        _u3(worldShader, "lightDir", { 0.3f,-1.f,0.4f });
        _u3(worldShader, "baseColor", baseCol);
        for (auto& m : meshes) {
            if (m.texID) {
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.texID);
                glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 1);
                glUniform1i(glGetUniformLocation(worldShader, "tex"), 0);
            }
            else glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 0);
            glBindVertexArray(m.VAO);
            glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
        }
    }

    void _drawObj(const EdObj& o, const glm::mat4& view, const glm::mat4& proj, bool sel)
    {
        if (!o.loaded) return;
        if (o.isEnemy || o.isSpawn) {
            glm::vec4 c = o.isSpawn ? glm::vec4(0.1f, 1.f, 0.4f, 1.f)
                : (o.enemyType == "zombie" ? glm::vec4(1.f, 0.15f, 0.1f, 1.f)
                    : (o.enemyType == "soldier" ? glm::vec4(0.2f, 0.5f, 1.f, 1.f)
                        : glm::vec4(0.9f, 0.1f, 0.9f, 1.f)));
            _drawMarker(o.pos, proj * view, c, 1.f);
            return;
        }
        if (sel) {
            glUseProgram(hlShader);
            glm::mat4 hm = glm::scale(o.matrix(), { 1.02f,1.02f,1.02f });
            _u4(hlShader, "model", hm); _u4(hlShader, "view", view); _u4(hlShader, "projection", proj);
            glUniform3f(glGetUniformLocation(hlShader, "hlColor"), 1.f, 0.55f, 0.05f);
            glDisable(GL_DEPTH_TEST);
            for (auto& m : o.meshes) { glBindVertexArray(m.VAO); glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0); }
            glEnable(GL_DEPTH_TEST);
        }
        _drawWorld(o.meshes, o.matrix(), view, proj, { 0.78f,0.74f,0.68f });
        if (sel) _drawBBox(o.pos, o.scl, proj * view, { 1.f,0.55f,0.05f,1.f });
    }

    void _drawMarker(const glm::vec3& p, const glm::mat4& vp, glm::vec4 col, float s) {
        float v[] = { p.x - s,p.y,p.z,p.x + s,p.y,p.z,p.x,p.y - s,p.z,p.x,p.y + s,p.z,p.x,p.y,p.z - s,p.x,p.y,p.z + s };
        GLuint va, vb; glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
        glBindVertexArray(va); glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0); glEnableVertexAttribArray(0);
        glUseProgram(lineShader);
        glUniformMatrix4fv(glGetUniformLocation(lineShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform4fv(glGetUniformLocation(lineShader, "color"), 1, glm::value_ptr(col));
        glLineWidth(3.f); glDrawArrays(GL_LINES, 0, 6); glLineWidth(1.f);
        glDeleteVertexArrays(1, &va); glDeleteBuffers(1, &vb);
    }

    void _drawBBox(const glm::vec3& c, const glm::vec3& s, const glm::mat4& vp, glm::vec4 col) {
        glm::vec3 h = glm::abs(s) * 1.1f + 0.1f;
        float x0 = c.x - h.x, x1 = c.x + h.x, y0 = c.y, y1 = c.y + h.y * 2.f, z0 = c.z - h.z, z1 = c.z + h.z;
        float v[] = { x0,y0,z0,x1,y0,z0,x1,y0,z0,x1,y0,z1,x1,y0,z1,x0,y0,z1,x0,y0,z1,x0,y0,z0,
                   x0,y1,z0,x1,y1,z0,x1,y1,z0,x1,y1,z1,x1,y1,z1,x0,y1,z1,x0,y1,z1,x0,y1,z0,
                   x0,y0,z0,x0,y1,z0,x1,y0,z0,x1,y1,z0,x1,y0,z1,x1,y1,z1,x0,y0,z1,x0,y1,z1 };
        GLuint va, vb; glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
        glBindVertexArray(va); glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0); glEnableVertexAttribArray(0);
        glUseProgram(lineShader);
        glUniformMatrix4fv(glGetUniformLocation(lineShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform4fv(glGetUniformLocation(lineShader, "color"), 1, glm::value_ptr(col));
        glDrawArrays(GL_LINES, 0, 24);
        glDeleteVertexArrays(1, &va); glDeleteBuffers(1, &vb);
    }

    void _drawGrid(const glm::mat4& vp) {
        glUseProgram(lineShader);
        glUniformMatrix4fv(glGetUniformLocation(lineShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform4f(glGetUniformLocation(lineShader, "color"), 0.24f, 0.25f, 0.28f, 0.5f);
        glBindVertexArray(gridVAO); glDrawArrays(GL_LINES, 0, gridVertCount);
        float ax[] = { -200,0,0,200,0,0,0,0,-200,0,0,200 };
        GLuint va, vb; glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
        glBindVertexArray(va); glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, sizeof(ax), ax, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0); glEnableVertexAttribArray(0);
        glLineWidth(2.f);
        glUniform4f(glGetUniformLocation(lineShader, "color"), 0.7f, 0.15f, 0.15f, 1.f);
        glDrawArrays(GL_LINES, 0, 2);
        glUniform4f(glGetUniformLocation(lineShader, "color"), 0.15f, 0.35f, 0.8f, 1.f);
        glDrawArrays(GL_LINES, 2, 2);
        glLineWidth(1.f);
        glDeleteVertexArrays(1, &va); glDeleteBuffers(1, &vb);
    }

    // ──────────────────────────────────────────────────────────
    //  Shader compile helpers
    // ──────────────────────────────────────────────────────────
    GLuint _pair(const char* vs, const char* fs) {
        auto cc = [](GLenum t, const char* s)->GLuint {
            GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh);
            GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, nullptr, l); printf("[ED_SH] %s\n", l); }
            return sh;
            };
        GLuint v = cc(GL_VERTEX_SHADER, vs), f = cc(GL_FRAGMENT_SHADER, fs);
        GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f); return p;
    }
    void _u4(GLuint p, const char* n, const glm::mat4& m) { glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, glm::value_ptr(m)); }
    void _u3(GLuint p, const char* n, glm::vec3 v) { glUniform3fv(glGetUniformLocation(p, n), 1, glm::value_ptr(v)); }

    GLuint _compileWorld() {
        return _pair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;layout(location=1)in vec3 aNorm;layout(location=2)in vec2 aUV;
out vec3 vN;out vec2 vUV;
uniform mat4 model,view,projection;
void main(){vN=mat3(transpose(inverse(model)))*aNorm;vUV=aUV;gl_Position=projection*view*model*vec4(aPos,1.);})",
R"(#version 330 core
in vec3 vN;in vec2 vUV;out vec4 FC;
uniform vec3 lightDir,baseColor;uniform int hasTexture;uniform sampler2D tex;
void main(){vec3 c=hasTexture>0?texture(tex,vUV).rgb:baseColor;float d=max(dot(normalize(vN),normalize(-lightDir)),0.);FC=vec4((0.25+d*.75)*c,1.);})");
    }
    GLuint _compileLine() {
        return _pair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;uniform mat4 vp;void main(){gl_Position=vp*vec4(aPos,1.);})",
R"(#version 330 core
out vec4 FC;uniform vec4 color;void main(){FC=color;})");
    }
    GLuint _compileHL() {
        return _pair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;uniform mat4 model,view,projection;
void main(){gl_Position=projection*view*model*vec4(aPos,1.);})",
R"(#version 330 core
out vec4 FC;uniform vec3 hlColor;void main(){FC=vec4(hlColor,0.6);})");
    }
    GLuint _compileGizmo() {
        return _pair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;uniform mat4 vp;void main(){gl_Position=vp*vec4(aPos,1.);})",
R"(#version 330 core
out vec4 FC;uniform vec3 color;void main(){FC=vec4(color,1.);})");
    }

    void _makeGrid(float size, float step) {
        std::vector<float> v;
        for (float x = -size; x <= size; x += step) { v.push_back(x); v.push_back(0); v.push_back(-size); v.push_back(x); v.push_back(0); v.push_back(size); }
        for (float z = -size; z <= size; z += step) { v.push_back(-size); v.push_back(0); v.push_back(z); v.push_back(size); v.push_back(0); v.push_back(z); }
        gridVertCount = (int)v.size() / 3;
        glGenVertexArrays(1, &gridVAO); glGenBuffers(1, &gridVBO);
        glBindVertexArray(gridVAO); glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0); glEnableVertexAttribArray(0);
    }
    void _makeFloor() {
        float fv[] = { -200,0,-200,0,1,0,0,0,200,0,-200,0,1,0,1,0,200,0,200,0,1,0,1,1,
                    -200,0,-200,0,1,0,0,0,200,0,200,0,1,0,1,1,-200,0,200,0,1,0,0,1 };
        glGenVertexArrays(1, &floorVAO); glGenBuffers(1, &floorVBO);
        glBindVertexArray(floorVAO); glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(fv), fv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24); glEnableVertexAttribArray(2);
    }
    void _makeGizmoArrows() { /* геометрия стрелок в _drawGizmo через GL_STREAM_DRAW */ }

    void setStatus(const std::string& s) { statusMsg = s; statusTimer = 5.f; printf("[ED] %s\n", s.c_str()); }
};

inline MapEditor mapEditor;