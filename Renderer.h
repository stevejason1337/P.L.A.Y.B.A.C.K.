#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#include "Settings.h"
#include "AnimatedModel.h"
#include "Player.h"
#include "WeaponManager.h"

inline const char* WORLD_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
out vec3 vNorm; out vec2 vUV;
uniform mat4 model,view,projection;
void main(){
    vNorm=mat3(transpose(inverse(model)))*aNorm;
    vUV=aUV;
    gl_Position=projection*view*model*vec4(aPos,1.0);
})";

inline const char* WORLD_FRAG = R"(
#version 330 core
in vec3 vNorm; in vec2 vUV;
out vec4 FragColor;
uniform vec3 lightDir,baseColor;
uniform bool hasTexture;
uniform sampler2D tex;
void main(){
    vec3 col=hasTexture?texture(tex,vUV).rgb:baseColor;
    float d=max(dot(normalize(vNorm),normalize(-lightDir)),0.0);
    FragColor=vec4((0.35+d*0.65)*col,1.0);
})";

inline const char* GUN_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aBoneIDs;
layout(location=4) in vec4 aWeights;
out vec3 vNorm; out vec2 vUV;
uniform mat4 model,view,projection;
uniform bool skinned;
uniform mat4 bones[100];
void main(){
    vec4 pos=vec4(aPos,1.0); vec4 nor=vec4(aNorm,0.0);
    if(skinned){
        mat4 skin=mat4(0.0);
        for(int i=0;i<4;i++){
            int id=int(aBoneIDs[i]);
            if(id>=0&&aWeights[i]>0.0) skin+=bones[id]*aWeights[i];
        }
        float ws=aWeights.x+aWeights.y+aWeights.z+aWeights.w;
        if(ws<0.001) skin=mat4(1.0);
        pos=skin*pos; nor=skin*nor;
    }
    vNorm=mat3(transpose(inverse(model)))*nor.xyz;
    vUV=aUV;
    gl_Position=projection*view*model*pos;
})";

inline const char* GUN_FRAG = R"(
#version 330 core
in vec3 vNorm; in vec2 vUV;
out vec4 FragColor;
uniform bool hasTexture;
uniform sampler2D tex;
uniform vec3 gunColor;
uniform float flash;
void main(){
    vec3 col=hasTexture?texture(tex,vUV).rgb:gunColor;
    float d=max(dot(normalize(vNorm),vec3(0.4,0.7,0.3)),0.0);
    FragColor=vec4((0.4+d*0.6)*col+vec3(flash),1.0);
})";

inline const char* DOT_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 mvp;
void main(){gl_Position=mvp*vec4(aPos,1.0);})";

inline const char* DOT_FRAG = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 color;
void main(){FragColor=color;})";

inline unsigned int buildShader(const char* v, const char* f)
{
    auto compile = [](unsigned int t, const char* s) {
        unsigned int sh = glCreateShader(t);
        glShaderSource(sh, 1, &s, NULL); glCompileShader(sh);
        int ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, NULL, l); std::cerr << "[SHADER]" << l; }
        return sh;
        };
    unsigned int vs = compile(GL_VERTEX_SHADER, v), fs = compile(GL_FRAGMENT_SHADER, f);
    unsigned int p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}
inline void uMat4(unsigned int p, const char* n, const glm::mat4& m) { glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, glm::value_ptr(m)); }
inline void uVec3(unsigned int p, const char* n, const glm::vec3& v) { glUniform3fv(glGetUniformLocation(p, n), 1, glm::value_ptr(v)); }
inline void uVec4(unsigned int p, const char* n, const glm::vec4& v) { glUniform4fv(glGetUniformLocation(p, n), 1, glm::value_ptr(v)); }
inline void uFloat(unsigned int p, const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
inline void uBool(unsigned int p, const char* n, bool v) { glUniform1i(glGetUniformLocation(p, n), (int)v); }

inline unsigned int makeDotVAO()
{
    float v[] = { -.05f,-.05f,0,.05f,-.05f,0,.05f,.05f,0,-.05f,-.05f,0,.05f,.05f,0,-.05f,.05f,0 };
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    return VAO;
}

extern bool isADS;
inline constexpr float ADS_SPEED = 8.f;

struct Renderer
{
    unsigned int worldShader = 0, gunShader = 0, dotShader = 0;
    unsigned int dotVAO = 0, floorVAO = 0, floorVBO = 0;
    int   lastFireCounter = 0;
    bool  reloadStarted = false;
    float reloadTimer = 0.f;
    float reloadDuration = 2.5f;

    void init()
    {
        worldShader = buildShader(WORLD_VERT, WORLD_FRAG);
        gunShader = buildShader(GUN_VERT, GUN_FRAG);
        dotShader = buildShader(DOT_VERT, DOT_FRAG);
        dotVAO = makeDotVAO();

        float fv[] = {
            -50,0,-50,0,1,0,0,0, 50,0,-50,0,1,0,1,0, 50,0,50,0,1,0,1,1,
            -50,0,-50,0,1,0,0,0, 50,0,50,0,1,0,1,1, -50,0,50,0,1,0,0,1
        };
        glGenVertexArrays(1, &floorVAO); glGenBuffers(1, &floorVBO);
        glBindVertexArray(floorVAO);
        glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(fv), fv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);              glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    }

    void onWeaponSwitch()
    {
        reloadStarted = false; reloadTimer = 0.f;
        lastFireCounter = fireAnimCounter;
    }

    void finishReload(AnimatedModel& gm, const std::string& idleAnim)
    {
        gun.ammo = weaponManager.activeDef().maxAmmo;
        gun.reloading = false; gun.shootCooldown = 0.f;
        reloadStarted = false; reloadTimer = 0.f;
        gm.animDone = false; gm.looping = true; gm.curAnim = "";
        if (!idleAnim.empty() && gm.hasAnim(idleAnim)) gm.play(idleAnim, true);
        std::cout << "[RELOAD] Done. Ammo:" << gun.ammo << "\n";
    }

    void updateGunAnim(AnimatedModel& gm, float dt)
    {
        const WeaponDef& def = weaponManager.activeDef();

        if (fireAnimCounter != lastFireCounter) {
            lastFireCounter = fireAnimCounter;
            int v = fireAnimCounter % 3;
            const std::string& fa = (v == 0) ? def.animFire : (v == 1) ? def.animFire001 : def.animFire002;
            if (!fa.empty() && gm.hasAnim(fa)) gm.playOnce(fa, def.animIdle);
        }

        if (gun.reloading) {
            if (!reloadStarted) {
                reloadTimer = 0.f;
                const std::string& ra = gun.reloadFull ? def.animReloadFull : def.animReloadEasy;
                if (!ra.empty() && gm.hasAnim(ra)) {
                    gm.playOnce(ra, def.animIdle);
                    std::cout << "[RELOAD] Anim: " << ra << "\n";
                }
                else std::cout << "[RELOAD] Anim NOT FOUND — timer fallback\n";
                reloadStarted = true;
            }
            reloadTimer += dt;
            if (gm.isDone() || reloadTimer >= reloadDuration) {
                if (reloadTimer >= reloadDuration) std::cout << "[RELOAD] Timeout!\n";
                finishReload(gm, def.animIdle);
            }
        }

        if (!gun.reloading) {
            bool moving = glm::length(glm::vec2(player.vel.x, player.vel.z)) > 0.5f && player.onGround;
            if (gm.curAnim == def.animIdle && moving && !def.animWalk.empty() && gm.hasAnim(def.animWalk))
                gm.play(def.animWalk, true);
            if (!def.animWalk.empty() && gm.curAnim == def.animWalk && !moving && gm.hasAnim(def.animIdle))
                gm.play(def.animIdle, true);
        }

        gm.update(dt);
    }

    void drawScene(
        const std::vector<GPUMesh>& mapMeshes,
        AnimatedModel& gm,
        glm::mat4 mapT,
        const glm::vec3& camPos,
        const glm::vec3& cf,
        const glm::vec3& cu)
    {
        // ADS
        float adsTarget = isADS ? 1.f : 0.f;
        gun.adsProgress += (adsTarget - gun.adsProgress) * ADS_SPEED * (1.f / 60.f);
        if (gun.adsProgress < 0.001f) gun.adsProgress = 0.f;
        if (gun.adsProgress > 0.999f) gun.adsProgress = 1.f;
        float adsFOV = glm::mix(FOV, FOV * 0.6f, gun.adsProgress);
        glm::mat4 view = glm::lookAt(camPos, camPos + cf, cu);
        glm::mat4 proj = glm::perspective(glm::radians(adsFOV),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);

        glEnable(GL_DEPTH_TEST);
        glUseProgram(worldShader);
        uMat4(worldShader, "view", view);
        uMat4(worldShader, "projection", proj);
        uVec3(worldShader, "lightDir", glm::vec3(.3f, -1.f, .4f));

        if (!mapMeshes.empty()) {
            uMat4(worldShader, "model", mapT);
            uVec3(worldShader, "baseColor", glm::vec3(.75f, .72f, .65f));
            for (auto& m : mapMeshes) {
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
        else {
            uMat4(worldShader, "model", glm::mat4(1.f));
            uVec3(worldShader, "baseColor", glm::vec3(.3f, .55f, .3f));
            glUniform1i(glGetUniformLocation(worldShader, "hasTexture"), 0);
            glBindVertexArray(floorVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glUseProgram(dotShader);
        for (auto& bh : bulletHoles) {
            glm::mat4 mvp = proj * view * glm::translate(glm::mat4(1.f), bh.pos);
            uMat4(dotShader, "mvp", mvp);
            uVec4(dotShader, "color", glm::vec4(.05f, .05f, .05f, bh.life / 5.f));
            glBindVertexArray(dotVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        if (!gm.meshes.empty()) {
            glClear(GL_DEPTH_BUFFER_BIT);
            glUseProgram(gunShader);
            // Оружие всегда рендерится с фиксированным FOV — иначе при зуме оно плывёт
            glm::mat4 projGun = glm::perspective(glm::radians(FOV),
                (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.05f, 5000.f);

            glm::vec3 right = glm::normalize(glm::cross(cf, cu));
            glm::vec3 up2 = glm::normalize(glm::cross(right, cf));

            bool moving = glm::length(glm::vec2(player.vel.x, player.vel.z)) > 0.5f;
            float bobX = moving ? sinf(gun.bobTimer) * 0.003f : 0.f;
            float bobY = moving ? cosf(gun.bobTimer * 2.f) * 0.002f : 0.f;
            if (moving && player.onGround) gun.bobTimer += 0.016f * 6.f;

            const WeaponDef& def = weaponManager.activeDef();

            // Универсальное положение — меняй GUN_OFFSET_* в Settings.h
            float adsP = gun.adsProgress;
            float bobMul = 1.f - adsP;

            // Только X двигается при ADS, всё остальное без изменений
            float offsetR = glm::mix(GUN_OFFSET_RIGHT + def.posRight, 0.0f, adsP);
            float offsetU = GUN_OFFSET_UP + def.posUp + bobY * bobMul + gun.recoilOffset;
            float offsetF = GUN_OFFSET_FWD + def.posFwd;

            glm::vec3 gPos = camPos
                + right * (offsetR + bobX * bobMul)
                + up2 * offsetU
                + cf * offsetF;

            glm::mat4 gMat(1.f);
            gMat[0] = glm::vec4(right, 0.f);
            gMat[1] = glm::vec4(up2, 0.f);
            gMat[2] = glm::vec4(-cf, 0.f);
            gMat[3] = glm::vec4(gPos, 1.f);
            gMat = glm::rotate(gMat, glm::radians(def.rotY), glm::vec3(0, 1, 0));
            gMat = glm::rotate(gMat, glm::radians(def.rotX), glm::vec3(1, 0, 0));
            gMat = glm::scale(gMat, glm::vec3(def.scale));

            uMat4(gunShader, "model", gMat);
            uMat4(gunShader, "view", view);
            uMat4(gunShader, "projection", projGun);
            uVec3(gunShader, "gunColor", glm::vec3(.4f, .4f, .42f));
            uFloat(gunShader, "flash", flashTimer > 0.f ? flashTimer * 4.f : 0.f);

            bool hasBones = !gm.boneFinal.empty() && gm.boneCount > 0;
            uBool(gunShader, "skinned", hasBones);
            if (hasBones) {
                int loc = glGetUniformLocation(gunShader, "bones");
                if (loc >= 0)
                    glUniformMatrix4fv(loc, (GLsizei)std::min((int)gm.boneFinal.size(), 100),
                        GL_FALSE, glm::value_ptr(gm.boneFinal[0]));
            }

            for (auto& m : gm.meshes) {
                if (m.texID) {
                    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.texID);
                    glUniform1i(glGetUniformLocation(gunShader, "hasTexture"), 1);
                    glUniform1i(glGetUniformLocation(gunShader, "tex"), 0);
                }
                else glUniform1i(glGetUniformLocation(gunShader, "hasTexture"), 0);
                glBindVertexArray(m.VAO);
                glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
    }
};