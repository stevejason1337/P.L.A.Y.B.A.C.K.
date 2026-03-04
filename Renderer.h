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
out vec3 vNorm;
out vec2 vUV;
out vec3 vWorldPos;
out float vFogDist;
uniform mat4 model, view, projection;
uniform mat3 normalMatrix;   // передаём снаружи — не считаем в шейдере
uniform vec3 camPos;
void main(){
    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNorm     = normalMatrix * aNorm;
    vUV       = aUV;
    vec4 viewPos = view * worldPos;
    vFogDist  = -viewPos.z;
    gl_Position = projection * viewPos;
})";

inline const char* WORLD_FRAG = R"(
#version 330 core
in vec3 vNorm;
in vec2 vUV;
in vec3 vWorldPos;
in float vFogDist;
out vec4 FragColor;
uniform vec3  lightDir;
uniform vec3  baseColor;
uniform bool  hasTexture;
uniform sampler2D tex;
uniform vec3  fogColor;
uniform float fogStart;
uniform float fogEnd;

void main(){
    vec3 albedo = hasTexture ? pow(texture(tex, vUV).rgb, vec3(2.2)) : baseColor;

    vec3 N = normalize(vNorm);
    vec3 L = normalize(-lightDir);

    // Диффузный свет
    float NdotL = max(dot(N, L), 0.0);

    // Ambient — нейтральный тёплый, без синевы
    float ambStr = 0.30;
    vec3  ambCol = vec3(0.90, 0.85, 0.78);  // тёплый белый

    // Солнечный свет — тёплый жёлтый
    vec3  sunCol = vec3(1.0, 0.95, 0.85);
    float sunStr = NdotL * 0.65;

    // Небольшой заполняющий свет снизу
    float fillStr = max(dot(N, vec3(0.0, -1.0, 0.0)), 0.0) * 0.08;

    // AO — вертикальные грани чуть темнее горизонтальных
    float ao = 0.80 + 0.20 * abs(N.y);

    vec3 lit = albedo * (ambCol * ambStr + sunCol * sunStr + vec3(fillStr)) * ao;

    // Тонмаппинг — убирает пересвет
    lit = lit / (lit + vec3(0.55));

    // Туман
    float fogT = clamp((vFogDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogT = fogT * fogT;
    lit = mix(lit, fogColor, fogT);

    // Гамма
    lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));

    FragColor = vec4(lit, 1.0);
})";

inline const char* GUN_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aBoneIDs;
layout(location=4) in vec4 aWeights;
out vec3 vNorm; out vec2 vUV;
uniform mat4 model, view, projection;
uniform mat3 normalMatrix;
uniform bool skinned;
uniform mat4 bones[100];
void main(){
    vec4 pos = vec4(aPos, 1.0);
    vec3 nor = aNorm;
    if(skinned){
        mat4 skin = mat4(0.0);
        for(int i=0;i<4;i++){
            int id = int(aBoneIDs[i]);
            if(id >= 0 && aWeights[i] > 0.0) skin += bones[id] * aWeights[i];
        }
        float ws = aWeights.x+aWeights.y+aWeights.z+aWeights.w;
        if(ws < 0.001) skin = mat4(1.0);
        pos = skin * pos;
        nor = mat3(skin) * nor;
    }
    vNorm = normalMatrix * nor;
    vUV   = aUV;
    gl_Position = projection * view * model * pos;
})";

inline const char* GUN_FRAG = R"(
#version 330 core
in vec3 vNorm; in vec2 vUV;
out vec4 FragColor;
uniform bool  hasTexture;
uniform sampler2D tex;
uniform vec3  gunColor;
uniform float flash;
void main(){
    vec3 albedo = hasTexture ? pow(texture(tex,vUV).rgb, vec3(2.2)) : gunColor;
    vec3 N = normalize(vNorm);

    // Один мягкий источник — как в помещении RE7
    vec3 L = normalize(vec3(0.2, 0.6, 0.4));
    float diff = max(dot(N, L), 0.0);

    // Очень мягкий ambient — оружие в тени
    float amb = 0.15;

    // Никакого rim, никакого второго источника
    vec3 lit = albedo * (amb + diff * 0.40);

    // Агрессивный тонмаппинг — давит пересвет
    lit = lit / (lit + vec3(0.8));

    // Гамма
    lit = pow(max(lit, vec3(0.0)), vec3(1.0/2.2));

    // Вспышка выстрела
    lit += vec3(flash * 0.15);

    FragColor = vec4(lit, 1.0);
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

// ── Пост-процессинг (мыльность + виньетка + RE7 стиль) ──
inline const char* POST_VERT = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }
)";

inline const char* POST_FRAG = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D screenTex;
uniform vec2 resolution;
void main(){
    vec2 uv  = vUV;
    vec2 px  = 1.0 / resolution;
    float blurR = 1.3;
    vec3 col = vec3(0.0);
    col += texture(screenTex, uv+vec2(-blurR,-blurR)*px).rgb;
    col += texture(screenTex, uv+vec2(     0,-blurR)*px).rgb*2.0;
    col += texture(screenTex, uv+vec2( blurR,-blurR)*px).rgb;
    col += texture(screenTex, uv+vec2(-blurR,     0)*px).rgb*2.0;
    col += texture(screenTex, uv                      ).rgb*4.0;
    col += texture(screenTex, uv+vec2( blurR,     0)*px).rgb*2.0;
    col += texture(screenTex, uv+vec2(-blurR, blurR)*px).rgb;
    col += texture(screenTex, uv+vec2(     0, blurR)*px).rgb*2.0;
    col += texture(screenTex, uv+vec2( blurR, blurR)*px).rgb;
    col /= 16.0;
    // Хроматический аберрейшн по краям
    float ab = length(uv-0.5)*0.0014;
    vec2 adir = normalize(uv-0.5+vec2(0.001))*ab;
    col.r = texture(screenTex, uv+adir).r;
    col.b = texture(screenTex, uv-adir).b;
    // Виньетка
    float v = length(uv-0.5)*1.65;
    col *= 1.0 - v*v*0.50;
    // Контраст
    col = col*1.05 - vec3(0.025);
    FragColor = vec4(max(col,vec3(0.0)), 1.0);
}
)";

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
    // Пост-процессинг FBO
    unsigned int postShader = 0;
    unsigned int fbo = 0, fboTex = 0, fboRBO = 0;
    unsigned int quadVAO = 0, quadVBO = 0;
    unsigned int dotVAO = 0, floorVAO = 0, floorVBO = 0;
    int   lastFireCounter = 0;
    bool  reloadStarted = false;
    float reloadTimer = 0.f;
    float reloadDuration = 2.5f;

    // ── Кешированные uniform locations (инициализируются один раз в init) ──
    struct WorldLocs { int model, view, proj, normalMat, lightDir, baseColor, hasTex, tex, fogColor, fogStart, fogEnd, camPos; } wl{};
    struct GunLocs { int model, view, proj, normalMat, skinned, bones, hasTex, tex, gunColor, flash; } gl{};
    struct DotLocs { int mvp, color; } dl{};

    void init()
    {
        worldShader = buildShader(WORLD_VERT, WORLD_FRAG);
        gunShader = buildShader(GUN_VERT, GUN_FRAG);
        dotShader = buildShader(DOT_VERT, DOT_FRAG);
        dotVAO = makeDotVAO();

        // ── FBO для пост-процессинга ──
        postShader = buildShader(POST_VERT, POST_FRAG);

        // Полноэкранный квад
        float quadV[] = {
            -1,-1, 0,0,  1,-1, 1,0,  1,1, 1,1,
            -1,-1, 0,0,  1, 1, 1,1, -1,1, 0,1
        };
        glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadV), quadV, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Создаём FBO
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &fboTex);
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
        glGenRenderbuffers(1, &fboRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, fboRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, SCR_WIDTH, SCR_HEIGHT);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fboRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

        // Кешируем uniform locations
        wl.model = glGetUniformLocation(worldShader, "model");
        wl.view = glGetUniformLocation(worldShader, "view");
        wl.proj = glGetUniformLocation(worldShader, "projection");
        wl.normalMat = glGetUniformLocation(worldShader, "normalMatrix");
        wl.lightDir = glGetUniformLocation(worldShader, "lightDir");
        wl.baseColor = glGetUniformLocation(worldShader, "baseColor");
        wl.hasTex = glGetUniformLocation(worldShader, "hasTexture");
        wl.tex = glGetUniformLocation(worldShader, "tex");
        wl.fogColor = glGetUniformLocation(worldShader, "fogColor");
        wl.fogStart = glGetUniformLocation(worldShader, "fogStart");
        wl.fogEnd = glGetUniformLocation(worldShader, "fogEnd");
        wl.camPos = glGetUniformLocation(worldShader, "camPos");

        gl.model = glGetUniformLocation(gunShader, "model");
        gl.view = glGetUniformLocation(gunShader, "view");
        gl.proj = glGetUniformLocation(gunShader, "projection");
        gl.normalMat = glGetUniformLocation(gunShader, "normalMatrix");
        gl.skinned = glGetUniformLocation(gunShader, "skinned");
        gl.bones = glGetUniformLocation(gunShader, "bones");
        gl.hasTex = glGetUniformLocation(gunShader, "hasTexture");
        gl.tex = glGetUniformLocation(gunShader, "tex");
        gl.gunColor = glGetUniformLocation(gunShader, "gunColor");
        gl.flash = glGetUniformLocation(gunShader, "flash");

        dl.mvp = glGetUniformLocation(dotShader, "mvp");
        dl.color = glGetUniformLocation(dotShader, "color");
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

    // Начало кадра — рисуем в FBO
    void beginFrame() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }

    // Конец кадра — применяем пост-процессинг и выводим на экран
    void endFrame() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(postShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glUniform1i(glGetUniformLocation(postShader, "screenTex"), 0);
        glUniform2f(glGetUniformLocation(postShader, "resolution"),
            (float)SCR_WIDTH, (float)SCR_HEIGHT);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
    }

    void drawScene(
        const std::vector<GPUMesh>& mapMeshes,
        AnimatedModel& gm,
        glm::mat4 mapT,
        const glm::vec3& camPos,
        const glm::vec3& cf,
        const glm::vec3& cu)
    {
        float adsTarget = isADS ? 1.f : 0.f;
        gun.adsProgress += (adsTarget - gun.adsProgress) * ADS_SPEED * (1.f / 60.f);
        if (gun.adsProgress < 0.001f) gun.adsProgress = 0.f;
        if (gun.adsProgress > 0.999f) gun.adsProgress = 1.f;
        float adsFOV = glm::mix(FOV, FOV * 0.6f, gun.adsProgress);
        glm::mat4 view = glm::lookAt(camPos, camPos + cf, cu);
        glm::mat4 proj = glm::perspective(glm::radians(adsFOV),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.15f, 2000.f);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        // Polygon offset уменьшает z-fighting на параллельных поверхностях
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);        // отсечение задних граней
        glCullFace(GL_BACK);
        glUseProgram(worldShader);
        glUniformMatrix4fv(wl.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(wl.proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3f(wl.lightDir, .3f, -1.f, .4f);
        glUniform3fv(wl.camPos, 1, glm::value_ptr(camPos));
        glUniform3f(wl.fogColor, 0.68f, 0.65f, 0.60f);  // тёплый бежевый туман
        glUniform1f(wl.fogStart, 15.f);
        glUniform1f(wl.fogEnd, 60.f);

        if (!mapMeshes.empty()) {
            glUniformMatrix4fv(wl.model, 1, GL_FALSE, glm::value_ptr(mapT));
            glm::mat3 nmWorld = glm::mat3(glm::transpose(glm::inverse(mapT)));
            glUniformMatrix3fv(wl.normalMat, 1, GL_FALSE, glm::value_ptr(nmWorld));
            glUniform3f(wl.baseColor, .75f, .72f, .65f);
            unsigned int lastTex = 0;
            for (auto& m : mapMeshes) {
                if (m.texID) {
                    if (m.texID != lastTex) {   // меняем текстуру только если изменилась
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, m.texID);
                        lastTex = m.texID;
                    }
                    glUniform1i(wl.hasTex, 1);
                    glUniform1i(wl.tex, 0);
                }
                else { glUniform1i(wl.hasTex, 0); lastTex = 0; }
                glBindVertexArray(m.VAO);
                glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
        else {
            glm::mat4 identity(1.f);
            glUniformMatrix4fv(wl.model, 1, GL_FALSE, glm::value_ptr(identity));
            glUniform3f(wl.baseColor, .3f, .55f, .3f);
            glUniform1i(wl.hasTex, 0);
            glBindVertexArray(floorVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        if (!bulletHoles.empty()) {
            glUseProgram(dotShader);
            glBindVertexArray(dotVAO);
            for (auto& bh : bulletHoles) {
                glm::mat4 mvp = proj * view * glm::translate(glm::mat4(1.f), bh.pos);
                glUniformMatrix4fv(dl.mvp, 1, GL_FALSE, glm::value_ptr(mvp));
                glUniform4f(dl.color, .05f, .05f, .05f, bh.life / 5.f);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        if (!gm.meshes.empty()) {
            glClear(GL_DEPTH_BUFFER_BIT);
            glDisable(GL_CULL_FACE);   // оружие рисуем без culling
            glUseProgram(gunShader);
            // FOV оружия отдельный — меньше = оружие крупнее визуально
            glm::mat4 projGun = glm::perspective(glm::radians(GUN_FOV),
                (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.01f, 100.f);

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

            glUniformMatrix4fv(gl.model, 1, GL_FALSE, glm::value_ptr(gMat));
            glUniformMatrix4fv(gl.view, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(gl.proj, 1, GL_FALSE, glm::value_ptr(projGun));
            glm::mat3 nmGun = glm::mat3(glm::transpose(glm::inverse(gMat)));
            glUniformMatrix3fv(gl.normalMat, 1, GL_FALSE, glm::value_ptr(nmGun));
            glUniform3f(gl.gunColor, .4f, .4f, .42f);
            glUniform1f(gl.flash, flashTimer > 0.f ? flashTimer * 4.f : 0.f);

            bool hasBones = !gm.boneFinal.empty() && gm.boneCount > 0;
            glUniform1i(gl.skinned, (int)hasBones);
            if (hasBones && gl.bones >= 0) {
                int bc = std::min(gm.boneCount, 100);
                glUniformMatrix4fv(gl.bones, bc, GL_FALSE, glm::value_ptr(gm.boneFinal[0]));
            }

            unsigned int lastTexG = 0;
            for (auto& m : gm.meshes) {
                if (m.texID) {
                    if (m.texID != lastTexG) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, m.texID);
                        lastTexG = m.texID;
                    }
                    glUniform1i(gl.hasTex, 1); glUniform1i(gl.tex, 0);
                }
                else { glUniform1i(gl.hasTex, 0); lastTexG = 0; }
                glBindVertexArray(m.VAO);
                glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
    }
};