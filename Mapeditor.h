#pragma once

// ══════════════════════════════════════════════
//  MapEditor.h  —  Far Cry 2/3 style map editor
//  Used by Editor.exe (editor_main.cpp)
//  Saves to maps/level.map  →  Game.exe reads it
// ══════════════════════════════════════════════

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

#include "Settings.h"
#include "ModelLoader.h"
#include "TextRenderer.h"
#include "AABB.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// ──────────────────────────────────────────────
//  Placed object
// ──────────────────────────────────────────────
struct EdObj
{
    std::string          modelPath;
    std::string          texDir;
    glm::vec3            pos = { 0,0,0 };
    glm::vec3            rot = { 0,0,0 };   // euler degrees
    glm::vec3            scl = { 1,1,1 };
    std::vector<GPUMesh> meshes;
    bool                 loaded = false;
    bool                 isSpawn = false;   // player spawn marker
    bool                 isEnemy = false;
    std::string          enemyType = "";
    float                enemyPatrolRadius = 8.f;

    glm::mat4 matrix() const
    {
        glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
        m = glm::rotate(m, glm::radians(rot.y), { 0,1,0 });
        m = glm::rotate(m, glm::radians(rot.x), { 1,0,0 });
        m = glm::rotate(m, glm::radians(rot.z), { 0,0,1 });
        m = glm::scale(m, scl);
        return m;
    }
};

// ──────────────────────────────────────────────
//  Palette item  (models you can place)
//  Add your own models here!
// ──────────────────────────────────────────────
struct PaletteEntry {
    std::string label;
    std::string modelPath;
    std::string texDir;
    glm::vec3   defScale = { 1,1,1 };
    bool        isSpawn = false;
};

inline std::vector<PaletteEntry> editorPalette = {
    { "[SPAWN]",  "",                                                       "",                                    {1,1,1}, true  },
    { "[ZOMBIE]",  "models/characters/walker/walker.fbx",                   "models/characters/walker/textures",   {0.01f,0.01f,0.01f}, false },
    { "[SOLDIER]", "models/characters/soldier/Ch35_nonPBR.fbx",             "models/characters/soldier/textures",  {0.01f,0.01f,0.01f}, false },
    { "[ZOMBIE2]", "models/characters/walker2/walker2.fbx.fbx",             "models/characters/walker2/textures",  {0.01f,0.01f,0.01f}, false },
    { "Cube",     "models/props/cube/cube.fbx",           "models/props/cube/textures",   {1,1,1}, false },
    { "House",    "models/props/house/house.fbx",         "models/props/house/textures",  {1,1,1}, false },
    { "Tree",     "models/props/tree/tree.fbx",           "models/props/tree/textures",   {1,1,1}, false },
    { "Rock",     "models/props/rock/rock.fbx",           "models/props/rock/textures",   {1,1,1}, false },
    { "Barrel",   "models/props/barrel/barrel.fbx",       "models/props/barrel/textures", {1,1,1}, false },
    { "Fence",    "models/props/fence/fence.fbx",         "models/props/fence/textures",  {1,1,1}, false },
    { "Crate",    "models/props/crate/crate.fbx",         "models/props/crate/textures",  {1,1,1}, false },
    { "Wall",     "models/props/wall/wall.fbx",           "models/props/wall/textures",   {1,1,1}, false },
    { "Lamp",     "models/props/lamp/lamp.fbx",           "models/props/lamp/textures",   {1,1,1}, false },
};

// ──────────────────────────────────────────────
//  Editor free-fly camera
// ──────────────────────────────────────────────
struct EdCam
{
    glm::vec3 pos = { 0,8,20 };
    float     yaw = -90.f;
    float     pitch = -20.f;
    float     speed = 20.f;
    glm::vec3 front = { 0,0,-1 };
    glm::vec3 up = { 0,1,0 };
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

// ══════════════════════════════════════════════
//  MapEditor
// ══════════════════════════════════════════════
struct MapEditor
{
    // State
    EdCam              cam;
    std::vector<EdObj> objects;
    int  selected = -1;
    int  palIdx = 0;      // current palette selection

    // Gizmo  0=Translate  1=Rotate  2=Scale
    int  gizmoMode = 0;

    // Snapping
    float gridSnap = 1.0f;
    float rotSnap = 15.f;
    bool  snapOn = true;

    // Display
    bool showGrid = true;

    // Map background (read-only terrain)
    std::vector<GPUMesh> mapMeshes;
    glm::mat4            mapT = glm::mat4(1.f);

    // File
    std::string saveFile = "maps/level.map";

    // Status bar
    std::string statusMsg = "Ready.";
    float       statusTimer = 0.f;

    // Shaders
    unsigned int worldShader = 0;
    unsigned int lineShader = 0;
    unsigned int hlShader = 0;   // highlight / selection

    // Grid VAO
    unsigned int gridVAO = 0, gridVBO = 0;
    int          gridVertCount = 0;

    // Fallback floor
    unsigned int floorVAO = 0, floorVBO = 0;

    // ──────────────────────────────────────────
    void init(const std::vector<GPUMesh>& map, glm::mat4 mT)
    {
        mapMeshes = map;
        mapT = mT;

        worldShader = compileWorld();
        lineShader = compileLine();
        hlShader = compileHL();

        makeGrid(200.f, 1.f);
        makeFloor();

        // Make sure save directory exists
#ifdef _WIN32
        system("if not exist maps mkdir maps");
#else
        system("mkdir -p maps");
#endif

        setStatus("Editor ready. RMB+WASD fly | Enter=Place | Del=Delete | Ctrl+S=Save | F5=Play");
    }

    // ──────────────────────────────────────────
    void update(GLFWwindow* w, float dt)
    {
        if (statusTimer > 0) statusTimer -= dt;
        flyCam(w, dt);
    }

    // ──────────────────────────────────────────
    void draw()
    {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;
        glm::mat4 view = cam.view();
        glm::mat4 proj = glm::perspective(glm::radians(60.f), W / H, 0.05f, 5000.f);
        glm::mat4 vp = proj * view;

        glClearColor(0.16f, 0.17f, 0.20f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        // Terrain
        if (!mapMeshes.empty())
            drawWorld(mapMeshes, mapT, view, proj, { 0.70f,0.68f,0.62f });
        else {
            // Fallback floor
            glUseProgram(worldShader);
            setUniform4(worldShader, "model", glm::mat4(1.f));
            setUniform4(worldShader, "view", view);
            setUniform4(worldShader, "projection", proj);
            setUniform3(worldShader, "lightDir", { 0.3f,-1.f,0.4f });
            setUniform3(worldShader, "baseColor", { 0.25f,0.28f,0.25f });
            glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 0);
            glBindVertexArray(floorVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // Grid
        if (showGrid) drawGrid(vp);

        // Placed objects
        for (int i = 0; i < (int)objects.size(); i++)
            drawObj(objects[i], view, proj, i == selected);

        // 2D UI
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawUI();
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    // ──────────────────────────────────────────
    //  Key handler
    // ──────────────────────────────────────────
    void onKey(GLFWwindow* w, int key, int action)
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

        bool ctrl = (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);

        // Mode switches
        if (!ctrl) {
            if (key == GLFW_KEY_G) { gizmoMode = 0; setStatus("Translate mode  (Arrows/Q/E to move)"); }
            if (key == GLFW_KEY_R) { gizmoMode = 1; setStatus("Rotate mode  ([ ] to rotate Y)"); }
            if (key == GLFW_KEY_T) { gizmoMode = 2; setStatus("Scale mode  (+/- to scale)"); }
        }

        // Save / Load
        if (ctrl && key == GLFW_KEY_S) { save(); setStatus("Saved: " + saveFile); }
        if (ctrl && key == GLFW_KEY_O) { load(); setStatus("Loaded: " + saveFile); }

        // Duplicate
        if (ctrl && key == GLFW_KEY_D && selected >= 0) {
            EdObj copy = objects[selected];
            copy.pos.x += gridSnap > 0 ? gridSnap : 1.f;
            objects.push_back(copy);
            selected = (int)objects.size() - 1;
            setStatus("Duplicated object.");
        }

        // Select all
        if (ctrl && key == GLFW_KEY_A) { selected = -1; setStatus("Deselected all."); }

        // Delete
        if (key == GLFW_KEY_DELETE && selected >= 0) {
            objects.erase(objects.begin() + selected);
            selected = std::min(selected, (int)objects.size() - 1);
            setStatus("Deleted.");
        }

        // Place
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_F) placeObject();

        // Deselect
        if (key == GLFW_KEY_ESCAPE) selected = -1;

        // Grid toggle
        if (key == GLFW_KEY_TAB) { showGrid = !showGrid; }

        // Snap toggle
        if (key == GLFW_KEY_HOME) {
            snapOn = !snapOn;
            setStatus(snapOn ? "Snap ON" : "Snap OFF");
        }

        // Palette
        if (key == GLFW_KEY_PAGE_UP)   palIdx = std::max(0, palIdx - 1);
        if (key == GLFW_KEY_PAGE_DOWN) palIdx = std::min((int)editorPalette.size() - 1, palIdx + 1);

        // Transform selected object
        if (selected >= 0) {
            EdObj& o = objects[selected];
            float step = snapOn && gridSnap > 0 ? gridSnap : 0.1f;
            float rStep = snapOn ? rotSnap : 1.f;

            if (gizmoMode == 0) {  // Translate
                if (key == GLFW_KEY_LEFT)  o.pos.x -= step;
                if (key == GLFW_KEY_RIGHT) o.pos.x += step;
                if (key == GLFW_KEY_UP)    o.pos.z -= step;
                if (key == GLFW_KEY_DOWN)  o.pos.z += step;
                if (key == GLFW_KEY_Q)     o.pos.y += step;
                if (key == GLFW_KEY_E)     o.pos.y -= step;
            }
            else if (gizmoMode == 1) {  // Rotate
                if (key == GLFW_KEY_LEFT || key == GLFW_KEY_LEFT_BRACKET)  o.rot.y -= rStep;
                if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_RIGHT_BRACKET) o.rot.y += rStep;
                if (key == GLFW_KEY_UP)    o.rot.x -= rStep;
                if (key == GLFW_KEY_DOWN)  o.rot.x += rStep;
                if (key == GLFW_KEY_Q)     o.rot.z += rStep;
                if (key == GLFW_KEY_E)     o.rot.z -= rStep;
            }
            else if (gizmoMode == 2) {  // Scale
                if (key == GLFW_KEY_KP_ADD || key == GLFW_KEY_EQUAL) o.scl *= 1.1f;
                if (key == GLFW_KEY_KP_SUBTRACT || key == GLFW_KEY_MINUS) o.scl *= 0.9f;
                if (key == GLFW_KEY_UP)    o.scl.y *= 1.1f;
                if (key == GLFW_KEY_DOWN)  o.scl.y *= 0.9f;
                if (key == GLFW_KEY_LEFT)  o.scl.x *= 0.9f;
                if (key == GLFW_KEY_RIGHT) o.scl.x *= 1.1f;
            }

            // Snap to ground
            if (key == GLFW_KEY_Z) {
                float gy = getGroundY(o.pos);
                if (gy != std::numeric_limits<float>::lowest()) {
                    o.pos.y = gy;
                    setStatus("Snapped to ground.");
                }
            }
        }
    }

    // ──────────────────────────────────────────
    void onMouseButton(GLFWwindow* w, int btn, int action)
    {
        double mx, my;
        glfwGetCursorPos(w, &mx, &my);

        // RMB = fly camera
        if (btn == GLFW_MOUSE_BUTTON_RIGHT) {
            cam.rmb = (action == GLFW_PRESS);
            cam.first = true;
            glfwSetInputMode(w, GLFW_CURSOR,
                cam.rmb ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            return;
        }

        // LMB click
        if (btn == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !cam.rmb) {
            // Left panel click
            if (mx < 230) {
                int pi = ((int)my - 66) / 26;
                if (pi >= 0 && pi < (int)editorPalette.size()) {
                    palIdx = pi;
                    setStatus("Selected: " + editorPalette[palIdx].label);
                }
                return;
            }
            // 3D viewport pick
            pickObject(w, mx, my);
        }
    }

    void onMouseMove(GLFWwindow*, double x, double y)
    {
        if (!cam.rmb) return;
        if (cam.first) { cam.lx = x; cam.ly = y; cam.first = false; return; }
        cam.yaw += (float)(x - cam.lx) * 0.15f;
        cam.pitch = glm::clamp(cam.pitch + (float)(cam.ly - y) * 0.15f, -89.f, 89.f);
        cam.lx = x; cam.ly = y;
        cam.updateFront();
    }

    void onScroll(double, double dy)
    {
        cam.speed = glm::clamp(cam.speed + (float)dy * 2.f, 1.f, 300.f);
    }

    // ──────────────────────────────────────────
    //  Save / Load  — simple text format
    // ──────────────────────────────────────────
    void save()
    {
        std::ofstream f(saveFile);
        if (!f) { setStatus("ERROR: cannot save " + saveFile); return; }
        f << "# 3D Engine Map  version 1\n";
        for (auto& o : objects) {
            f << "obj\n";
            f << "model " << (o.modelPath.empty() ? "SPAWN" : o.modelPath) << "\n";
            f << "tex " << (o.texDir.empty() ? "NONE" : o.texDir) << "\n";
            f << "pos " << o.pos.x << " " << o.pos.y << " " << o.pos.z << "\n";
            f << "rot " << o.rot.x << " " << o.rot.y << " " << o.rot.z << "\n";
            f << "scl " << o.scl.x << " " << o.scl.y << " " << o.scl.z << "\n";
            if (o.isEnemy)
                f << "enemy " << o.enemyType << " " << o.enemyPatrolRadius << "\n";
            f << "end\n";
        }
        std::cout << "[EDITOR] Saved " << objects.size() << " objects → " << saveFile << "\n";
    }

    void load()
    {
        std::ifstream f(saveFile);
        if (!f) { setStatus("No map file: " + saveFile); return; }
        objects.clear(); selected = -1;
        std::string line;
        EdObj cur; bool building = false;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line); std::string cmd; ss >> cmd;
            if (cmd == "obj") { building = true; cur = EdObj(); }
            else if (cmd == "model" && building) {
                std::string p; ss >> p;
                if (p == "SPAWN") { cur.modelPath = ""; cur.isSpawn = true; }
                else { cur.modelPath = p; cur.isSpawn = false; }
            }
            else if (cmd == "tex" && building) {
                std::string t; ss >> t;
                cur.texDir = (t == "NONE") ? "" : t;
            }
            else if (cmd == "pos" && building) ss >> cur.pos.x >> cur.pos.y >> cur.pos.z;
            else if (cmd == "rot" && building) ss >> cur.rot.x >> cur.rot.y >> cur.rot.z;
            else if (cmd == "scl" && building) ss >> cur.scl.x >> cur.scl.y >> cur.scl.z;
            else if (cmd == "enemy" && building) { cur.isEnemy = true; ss >> cur.enemyType >> cur.enemyPatrolRadius; }
            else if (cmd == "end" && building) {
                if (!cur.isSpawn && !cur.modelPath.empty())
                    cur.meshes = loadModel(cur.modelPath, cur.texDir, glm::mat4(1.f), false);
                cur.loaded = true;
                objects.push_back(cur);
                building = false;
            }
        }
        setStatus("Loaded " + std::to_string(objects.size()) + " objects.");
    }

    // Returns spawn position for Game.exe
    glm::vec3 getSpawnPos() const
    {
        for (auto& o : objects) if (o.isSpawn) return o.pos;
        if (!objects.empty()) return objects[0].pos + glm::vec3(0, 2, 0);
        return glm::vec3(0, 5, 0);
    }

private:
    // ──────────────────────────────────────────
    void flyCam(GLFWwindow* w, float dt)
    {
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

    void placeObject()
    {
        if (palIdx >= (int)editorPalette.size()) return;
        auto& pal = editorPalette[palIdx];
        EdObj o;
        o.modelPath = pal.modelPath;
        o.texDir = pal.texDir;
        o.scl = pal.defScale;
        o.isSpawn = pal.isSpawn;
        if (pal.label == "[ZOMBIE]") { o.isEnemy = true; o.enemyType = "zombie"; }
        if (pal.label == "[SOLDIER]") { o.isEnemy = true; o.enemyType = "soldier"; }
        if (pal.label == "[ZOMBIE2]") { o.isEnemy = true; o.enemyType = "zombie2"; }

        // Raycast from camera to find placement point
        glm::vec3 hitPos;
        if (shootRay(cam.pos, cam.front, hitPos)) {
            o.pos = hitPos;
        }
        else {
            o.pos = cam.pos + cam.front * 10.f;
            o.pos.y = 0.f;
        }

        // Grid snap
        if (snapOn && gridSnap > 0) {
            o.pos.x = roundf(o.pos.x / gridSnap) * gridSnap;
            o.pos.z = roundf(o.pos.z / gridSnap) * gridSnap;
        }

        if (!o.isSpawn && !o.modelPath.empty())
            o.meshes = loadModel(o.modelPath, o.texDir, glm::mat4(1.f), false);
        o.loaded = true;
        objects.push_back(o);
        selected = (int)objects.size() - 1;
        setStatus("Placed: " + pal.label);
    }

    void pickObject(GLFWwindow*, double mx, double my)
    {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;
        float nx = (2.f * (float)mx / W) - 1.f;
        float ny = 1.f - (2.f * (float)my / H);
        glm::mat4 proj = glm::perspective(glm::radians(60.f), W / H, 0.05f, 5000.f);
        glm::vec4 rc = { nx,ny,-1.f,1.f };
        glm::vec4 re = glm::inverse(proj) * rc; re.z = -1; re.w = 0;
        glm::vec3 rd = glm::normalize(glm::vec3(glm::inverse(cam.view()) * re));

        float best = 1e9f; int bestI = -1;
        for (int i = 0; i < (int)objects.size(); i++) {
            glm::vec3 oc = cam.pos - objects[i].pos;
            float r = objects[i].isSpawn ? 0.8f : 1.5f;
            float b = glm::dot(oc, rd);
            float c = glm::dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc < 0) continue;
            float t = -b - sqrtf(disc);
            if (t > 0 && t < best) { best = t; bestI = i; }
        }
        selected = bestI;
        if (selected >= 0)
            setStatus("Selected: " + (objects[selected].isSpawn ? "[SPAWN]" : objects[selected].modelPath));
    }

    // ──────────────────────────────────────────
    //  Draw helpers
    // ──────────────────────────────────────────
    void drawWorld(const std::vector<GPUMesh>& meshes, glm::mat4 model,
        const glm::mat4& view, const glm::mat4& proj, glm::vec3 baseCol)
    {
        glUseProgram(worldShader);
        setUniform4(worldShader, "model", model);
        setUniform4(worldShader, "view", view);
        setUniform4(worldShader, "projection", proj);
        setUniform3(worldShader, "lightDir", { 0.3f,-1.f,0.4f });
        setUniform3(worldShader, "baseColor", baseCol);
        for (auto& m : meshes) {
            if (m.texID) {
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.texID);
                glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 1);
                glUniform1i(glGetUniformLocation(worldShader, "tex"), 0);
            }
            else {
                glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 0);
            }
            glBindVertexArray(m.VAO);
            glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
        }
    }

    void drawObj(EdObj& o, const glm::mat4& view, const glm::mat4& proj, bool sel)
    {
        if (!o.loaded) return;

        if (o.isEnemy) {
            glm::vec4 ec = (o.enemyType == "zombie") ? glm::vec4(0.9f, 0.2f, 0.1f, 1.f)
                : (o.enemyType == "soldier") ? glm::vec4(0.2f, 0.5f, 1.f, 1.f)
                : glm::vec4(0.8f, 0.1f, 0.8f, 1.f);
            drawMarker(o.pos, proj * view, ec, 1.2f);
            return;
        }
        if (o.isSpawn) {
            drawMarker(o.pos, proj * view,
                sel ? glm::vec4(1.f, 1.f, 0.f, 1.f) : glm::vec4(0.f, 1.f, 0.5f, 1.f), 1.0f);
            if (sel) drawBBox(o.pos, { 0.5f,0.5f,0.5f }, proj * view, { 1.f,1.f,0.f,1.f });
            return;
        }

        // Selection highlight pass (slightly enlarged, bright)
        if (sel) {
            glUseProgram(hlShader);
            glm::mat4 hm = o.matrix();
            hm = glm::scale(hm, glm::vec3(1.02f));
            setUniform4(hlShader, "model", hm);
            setUniform4(hlShader, "view", view);
            setUniform4(hlShader, "projection", proj);
            glUniform3f(glGetUniformLocation(hlShader, "hlColor"), 1.f, 0.55f, 0.05f);
            glDisable(GL_DEPTH_TEST);
            for (auto& m : o.meshes) { glBindVertexArray(m.VAO); glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0); }
            glEnable(GL_DEPTH_TEST);
        }

        drawWorld(o.meshes, o.matrix(), view, proj, { 0.78f,0.74f,0.68f });

        if (sel) drawBBox(o.pos, o.scl, proj * view, { 1.f,0.55f,0.05f,1.f });
    }

    // Cross / marker for spawn point
    void drawMarker(const glm::vec3& p, const glm::mat4& vp, glm::vec4 col, float s)
    {
        float v[] = {
            p.x - s,p.y,p.z, p.x + s,p.y,p.z,
            p.x,p.y - s,p.z, p.x,p.y + s,p.z,
            p.x,p.y,p.z - s, p.x,p.y,p.z + s,
            // vertical pillar
            p.x,p.y,p.z,   p.x,p.y + 3.f,p.z
        };
        unsigned int va, vb;
        glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
        glBindVertexArray(va);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glUseProgram(lineShader);
        glUniformMatrix4fv(glGetUniformLocation(lineShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform4fv(glGetUniformLocation(lineShader, "color"), 1, glm::value_ptr(col));
        glLineWidth(3.f);
        glDrawArrays(GL_LINES, 0, 8);
        glLineWidth(1.f);
        glDeleteVertexArrays(1, &va); glDeleteBuffers(1, &vb);
    }

    void drawBBox(const glm::vec3& c, const glm::vec3& s, const glm::mat4& vp, glm::vec4 col)
    {
        glm::vec3 h = glm::abs(s) * 1.1f + 0.1f;
        float x0 = c.x - h.x, x1 = c.x + h.x, y0 = c.y, y1 = c.y + h.y * 2.f, z0 = c.z - h.z, z1 = c.z + h.z;
        float v[] = {
            x0,y0,z0, x1,y0,z0,  x1,y0,z0, x1,y0,z1,  x1,y0,z1, x0,y0,z1,  x0,y0,z1, x0,y0,z0,
            x0,y1,z0, x1,y1,z0,  x1,y1,z0, x1,y1,z1,  x1,y1,z1, x0,y1,z1,  x0,y1,z1, x0,y1,z0,
            x0,y0,z0, x0,y1,z0,  x1,y0,z0, x1,y1,z0,  x1,y0,z1, x1,y1,z1,  x0,y0,z1, x0,y1,z1
        };
        unsigned int va, vb;
        glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
        glBindVertexArray(va);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glUseProgram(lineShader);
        glUniformMatrix4fv(glGetUniformLocation(lineShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform4fv(glGetUniformLocation(lineShader, "color"), 1, glm::value_ptr(col));
        glDrawArrays(GL_LINES, 0, 24);
        glDeleteVertexArrays(1, &va); glDeleteBuffers(1, &vb);
    }

    void drawGrid(const glm::mat4& vp)
    {
        glUseProgram(lineShader);
        glUniformMatrix4fv(glGetUniformLocation(lineShader, "vp"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform4f(glGetUniformLocation(lineShader, "color"), 0.26f, 0.27f, 0.30f, 0.55f);
        glBindVertexArray(gridVAO);
        glDrawArrays(GL_LINES, 0, gridVertCount);

        // Axis lines
        float ax[] = { -200,0,0, 200,0,0,  0,0,-200, 0,0,200 };
        unsigned int va, vb;
        glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
        glBindVertexArray(va);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, sizeof(ax), ax, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glLineWidth(2.f);
        glUniform4f(glGetUniformLocation(lineShader, "color"), 0.75f, 0.20f, 0.20f, 1.f);
        glDrawArrays(GL_LINES, 0, 2);
        glUniform4f(glGetUniformLocation(lineShader, "color"), 0.20f, 0.40f, 0.85f, 1.f);
        glDrawArrays(GL_LINES, 2, 2);
        glLineWidth(1.f);
        glDeleteVertexArrays(1, &va); glDeleteBuffers(1, &vb);
    }

    // ──────────────────────────────────────────
    //  2D UI  — Far Cry style dark sidebar
    // ──────────────────────────────────────────
    void drawUI()
    {
        float W = (float)SCR_WIDTH, H = (float)SCR_HEIGHT;

        // ═══════════════════════════════════════════════════════
        //  LEFT PANEL — Palette + Properties
        // ═══════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(270, H), ImGuiCond_Always);
        ImGui::Begin("##left", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar);

        // ── Header ──────────────────────────────
        ImGui::SetCursorPosX(10);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.1f, 1.f));
        ImGui::Text("  FPS ENGINE — MAP EDITOR");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Palette ─────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.55f, 0.65f, 1.f));
        ImGui::Text("  PLACE OBJECTS   [PgUp/Dn]");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Category: Markers
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.38f, 0.45f, 1.f));
        ImGui::Text(" MARKERS"); ImGui::PopStyleColor();
        for (int i = 0; i < (int)editorPalette.size(); i++) {
            auto& pe = editorPalette[i];
            if (pe.label != "[SPAWN]") continue;
            bool sel = (palIdx == i);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.f));
            ImGui::Bullet(); ImGui::SameLine(); ImGui::PopStyleColor();
            char lbl[64]; snprintf(lbl, 64, "%s##p%d", pe.label.c_str(), i);
            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0, 20))) palIdx = i;
        }
        ImGui::Spacing();

        // Category: Enemies
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.38f, 0.45f, 1.f));
        ImGui::Text(" ENEMIES"); ImGui::PopStyleColor();
        const char* enemyLabels[] = { "[ZOMBIE]","[SOLDIER]","[ZOMBIE2]" };
        ImVec4 enemyColors[] = {
            ImVec4(0.9f,0.25f,0.15f,1.f),
            ImVec4(0.2f,0.5f,1.0f,1.f),
            ImVec4(0.8f,0.15f,0.8f,1.f)
        };
        for (int i = 0; i < (int)editorPalette.size(); i++) {
            auto& pe = editorPalette[i];
            bool isEnemy = false; int ci = 0;
            for (int k = 0; k < 3; k++) if (pe.label == enemyLabels[k]) { isEnemy = true; ci = k; break; }
            if (!isEnemy) continue;
            bool sel = (palIdx == i);
            ImGui::PushStyleColor(ImGuiCol_Text, enemyColors[ci]);
            ImGui::Text("  ⚠"); ImGui::SameLine(); ImGui::PopStyleColor();
            char lbl[64]; snprintf(lbl, 64, "%s##p%d", pe.label.c_str(), i);
            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0, 20))) palIdx = i;
        }
        ImGui::Spacing();

        // Category: Props
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.38f, 0.45f, 1.f));
        ImGui::Text(" PROPS"); ImGui::PopStyleColor();
        for (int i = 0; i < (int)editorPalette.size(); i++) {
            auto& pe = editorPalette[i];
            if (pe.isSpawn) continue;
            bool skip = false;
            for (int k = 0; k < 3; k++) if (pe.label == enemyLabels[k]) { skip = true; break; }
            if (skip) continue;
            bool sel = (palIdx == i);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.75f, 1.f, 1.f));
            ImGui::Bullet(); ImGui::SameLine(); ImGui::PopStyleColor();
            char lbl[64]; snprintf(lbl, 64, "%s##p%d", pe.label.c_str(), i);
            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0, 20))) palIdx = i;
        }

        ImGui::Separator();
        ImGui::Spacing();

        // ── Properties ──────────────────────────
        if (selected >= 0 && selected < (int)objects.size()) {
            auto& o = objects[selected];

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.55f, 0.65f, 1.f));
            ImGui::Text("  PROPERTIES"); ImGui::PopStyleColor();
            ImGui::Separator(); ImGui::Spacing();

            // Type badge
            if (o.isEnemy) {
                ImVec4 bc = (o.enemyType == "zombie") ? ImVec4(0.9f, 0.25f, 0.15f, 1.f)
                    : (o.enemyType == "soldier") ? ImVec4(0.2f, 0.5f, 1.f, 1.f)
                    : ImVec4(0.8f, 0.15f, 0.8f, 1.f);
                ImGui::PushStyleColor(ImGuiCol_Text, bc);
                ImGui::Text("  ⚠ ENEMY: %s", o.enemyType.c_str());
                ImGui::PopStyleColor();
            }
            else if (o.isSpawn) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.f));
                ImGui::Text("  ★ PLAYER SPAWN");
                ImGui::PopStyleColor();
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.75f, 1.f, 1.f));
                ImGui::Text("  ■ %s", o.modelPath.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();

            ImGui::PushItemWidth(200);
            ImGui::Text("Position");
            ImGui::DragFloat3("##pos", &o.pos.x, 0.1f);
            ImGui::Text("Rotation");
            ImGui::DragFloat3("##rot", &o.rot.x, 1.f);
            ImGui::Text("Scale");
            ImGui::DragFloat3("##scl", &o.scl.x, 0.01f, 0.001f, 100.f);
            if (o.isEnemy) {
                ImGui::Spacing();
                ImGui::Text("Patrol Radius");
                ImGui::SliderFloat("##patrol", &o.enemyPatrolRadius, 0.f, 30.f);
            }
            ImGui::PopItemWidth();
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.28f, 0.50f, 1.f));
            if (ImGui::Button("  Duplicate  [Ctrl+D]  ", ImVec2(-1, 0))) {
                EdObj cp = o; cp.pos.x += 2.f;
                objects.push_back(cp); selected = (int)objects.size() - 1;
                setStatus("Duplicated");
            }
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.10f, 0.10f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.15f, 0.15f, 1.f));
            if (ImGui::Button("  Delete  [Del]  ", ImVec2(-1, 0))) {
                objects.erase(objects.begin() + selected);
                selected = -1; setStatus("Deleted");
            }
            ImGui::PopStyleColor(2);
        }

        // ── Stats ────────────────────────────────
        ImGui::SetCursorPosY(H - 80);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.42f, 0.48f, 1.f));
        int ec = 0; for (auto& ob : objects) if (ob.isEnemy) ec++;
        ImGui::Text("  Objects: %d   Enemies: %d", (int)objects.size(), ec);
        ImGui::Text("  Cam: %.0f  %.0f  %.0f", cam.pos.x, cam.pos.y, cam.pos.z);
        ImGui::PopStyleColor();

        ImGui::End();

        // ═══════════════════════════════════════════════════════
        //  TOP TOOLBAR
        // ═══════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(270, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(W - 270, 46), ImGuiCond_Always);
        ImGui::Begin("##toolbar", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::SetCursorPosY(8);
        const char* gLabels[] = { "[G] Move","[R] Rotate","[T] Scale" };
        for (int i = 0; i < 3; i++) {
            bool a = (gizmoMode == i);
            if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.38f, 0.70f, 1.f));
            else  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 1.f));
            if (ImGui::Button(gLabels[i], ImVec2(88, 0))) gizmoMode = i;
            ImGui::PopStyleColor(); ImGui::SameLine();
        }
        ImGui::SameLine(0, 16);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.52f, 0.58f, 1.f));
        ImGui::Text("Snap: %.1f [Home]", snapOn ? gridSnap : 0.f);
        ImGui::PopStyleColor();

        ImGui::SameLine(W - 380);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.38f, 0.12f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.50f, 0.16f, 1.f));
        if (ImGui::Button("  Save  [Ctrl+S]  ")) { save(); setStatus("Map saved!"); }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.36f, 0.04f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.48f, 0.06f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.88f, 0.3f, 1.f));
        ImGui::Button("  F5 = Play Game  ");
        ImGui::PopStyleColor(3);
        ImGui::End();

        // ═══════════════════════════════════════════════════════
        //  STATUS BAR
        // ═══════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(270, H - 28), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(W - 270, 28), ImGuiCond_Always);
        ImGui::Begin("##status", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::SetCursorPosY(4);
        ImGui::PushStyleColor(ImGuiCol_Text, statusTimer > 0
            ? ImVec4(0.9f, 1.f, 0.4f, 1.f) : ImVec4(0.42f, 0.44f, 0.50f, 1.f));
        ImGui::Text("  %s", statusMsg.c_str());
        ImGui::PopStyleColor();
        ImGui::End();

        // ═══════════════════════════════════════════════════════
        //  HELP PANEL
        // ═══════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(W - 255, H - 250), ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.44f, 0.46f, 0.52f, 1.f));
        ImGui::Text("RMB + WASD    fly camera");
        ImGui::Text("Shift         fast fly");
        ImGui::Text("Enter / F     place object");
        ImGui::Text("Arrows / Q/E  move selected");
        ImGui::Text("Del           delete");
        ImGui::Text("Ctrl+D        duplicate");
        ImGui::Text("Z             snap to ground");
        ImGui::Text("Tab           toggle grid");
        ImGui::Text("Home          toggle snap");
        ImGui::Text("PgUp/Dn       change palette");
        ImGui::Text("Ctrl+S        save map");
        ImGui::Text("F5            play in game!");
        ImGui::PopStyleColor();
        ImGui::End();
    }

    void setStatus(const std::string& s) { statusMsg = s; statusTimer = 5.f; std::cout << "[ED] " << s << "\n"; }

    // ──────────────────────────────────────────
    //  Shader helpers
    // ──────────────────────────────────────────
    void setUniform4(unsigned int p, const char* n, const glm::mat4& m)
    {
        glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, glm::value_ptr(m));
    }
    void setUniform3(unsigned int p, const char* n, glm::vec3 v)
    {
        glUniform3fv(glGetUniformLocation(p, n), 1, glm::value_ptr(v));
    }

    unsigned int compilePair(const char* vs, const char* fs)
    {
        auto cc = [](unsigned int t, const char* s) {
            unsigned int sh = glCreateShader(t);
            glShaderSource(sh, 1, &s, NULL); glCompileShader(sh);
            int ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, NULL, l); std::cerr << "[ED_SH]" << l; }
            return sh;
            };
        unsigned int v = cc(GL_VERTEX_SHADER, vs), f = cc(GL_FRAGMENT_SHADER, fs);
        unsigned int p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f); return p;
    }

    unsigned int compileWorld() {
        return compilePair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;layout(location=1)in vec3 aNorm;layout(location=2)in vec2 aUV;
out vec3 vN;out vec2 vUV;
uniform mat4 model,view,projection;
void main(){vN=mat3(transpose(inverse(model)))*aNorm;vUV=aUV;gl_Position=projection*view*model*vec4(aPos,1.);})",
R"(#version 330 core
in vec3 vN;in vec2 vUV;out vec4 FC;
uniform vec3 lightDir,baseColor;uniform bool hasTexture;uniform sampler2D tex;
void main(){vec3 c=hasTexture?texture(tex,vUV).rgb:baseColor;float d=max(dot(normalize(vN),normalize(-lightDir)),0.);FC=vec4((0.3+d*.7)*c,1.);})");
    }
    unsigned int compileLine() {
        return compilePair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;uniform mat4 vp;
void main(){gl_Position=vp*vec4(aPos,1.);})",
R"(#version 330 core
out vec4 FC;uniform vec4 color;void main(){FC=color;})");
    }
    unsigned int compileHL() {
        return compilePair(
            R"(#version 330 core
layout(location=0)in vec3 aPos;uniform mat4 model,view,projection;
void main(){gl_Position=projection*view*model*vec4(aPos,1.);})",
R"(#version 330 core
out vec4 FC;uniform vec3 hlColor;void main(){FC=vec4(hlColor,1.);})");
    }

    void makeGrid(float size, float step)
    {
        std::vector<float> v;
        for (float x = -size; x <= size; x += step) { v.push_back(x); v.push_back(0); v.push_back(-size); v.push_back(x); v.push_back(0); v.push_back(size); }
        for (float z = -size; z <= size; z += step) { v.push_back(-size); v.push_back(0); v.push_back(z); v.push_back(size); v.push_back(0); v.push_back(z); }
        gridVertCount = (int)v.size() / 3;
        glGenVertexArrays(1, &gridVAO); glGenBuffers(1, &gridVBO);
        glBindVertexArray(gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
    }

    void makeFloor()
    {
        float fv[] = { -100,0,-100,0,1,0,0,0,100,0,-100,0,1,0,1,0,100,0,100,0,1,0,1,1,
                    -100,0,-100,0,1,0,0,0,100,0,100,0,1,0,1,1,-100,0,100,0,1,0,0,1 };
        glGenVertexArrays(1, &floorVAO); glGenBuffers(1, &floorVBO);
        glBindVertexArray(floorVAO);
        glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(fv), fv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    }
};

inline MapEditor mapEditor;