#pragma once
// ═══════════════════════════════════════════════════════════════
//  BloodFX.h  —  Брызги крови + декали на стенах
//
//  Использование:
//    1. #include "BloodFX.h"
//    2. В main.cpp после выстрела: bloodFX.spawnHit(hitPos, hitNormal, shootDir);
//    3. В игровом цикле: bloodFX.update(dt);
//    4. После рендера сцены: bloodFX.draw(view, proj);
//    5. При смерти врага: bloodFX.spawnDeath(enemyPos, shootDir);
// ═══════════════════════════════════════════════════════════════

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdlib>
#include <cmath>

// ───────────────────────────────────────────────────────────────
//  Константы
// ───────────────────────────────────────────────────────────────
static constexpr int   BLOOD_MAX_PARTICLES = 512;
static constexpr int   BLOOD_MAX_DECALS = 64;
static constexpr float BLOOD_GRAVITY = -14.f;
static constexpr float BLOOD_PARTICLE_LIFE = 0.55f;
static constexpr float BLOOD_DECAL_LIFE = 18.f;   // сколько секунд висит пятно

// ───────────────────────────────────────────────────────────────
//  Шейдеры
// ───────────────────────────────────────────────────────────────
static const char* BLOOD_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aLife;   // 0=мёртвая, 1=только родилась
uniform mat4 uVP;
uniform float uSize;
out float vLife;
void main(){
    vLife = aLife;
    gl_Position = uVP * vec4(aPos, 1.0);
    gl_PointSize = uSize * (0.4 + aLife * 0.6) * (1.0 / (1.0 + gl_Position.z * 0.08));
    gl_PointSize = clamp(gl_PointSize, 1.0, 18.0);
})";

static const char* BLOOD_FRAG = R"(
#version 330 core
in float vLife;
out vec4 FragColor;
void main(){
    // Круглая точка
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float d = dot(uv, uv);
    if(d > 1.0) discard;

    // Тёмно-красный, тускнеет к концу жизни
    float alpha = vLife * vLife * (1.0 - d * 0.4);
    vec3 col = mix(vec3(0.10, 0.0, 0.0), vec3(0.65, 0.03, 0.03), vLife);
    FragColor = vec4(col, alpha * 0.92);
})";

// Декаль-шейдер (quad в world space, ориентированный по нормали)
static const char* DECAL_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

static const char* DECAL_FRAG = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform float uAlpha;
void main(){
    // Процедурное пятно крови — несколько кругов + шум
    vec2 c = vUV * 2.0 - 1.0;
    float d = length(c);

    // Основная форма с зубчиками
    float angle = atan(c.y, c.x);
    float jagged = 0.08 * sin(angle * 7.0) + 0.05 * sin(angle * 13.0 + 1.2);
    float shape = smoothstep(0.85 + jagged, 0.60 + jagged, d);

    // Внутренние детали
    float inner = smoothstep(0.5, 0.0, d) * 0.35;
    float alpha = (shape + inner) * uAlpha;
    if(alpha < 0.02) discard;

    vec3 col = vec3(0.25, 0.0, 0.0) + vec3(0.1, 0.0, 0.0) * inner;
    FragColor = vec4(col, alpha);
})";

// ───────────────────────────────────────────────────────────────
//  Частица крови
// ───────────────────────────────────────────────────────────────
struct BloodParticle {
    glm::vec3 pos;
    glm::vec3 vel;
    float     life;      // 0..1, уменьшается
    float     maxLife;
    bool      active = false;
};

// ───────────────────────────────────────────────────────────────
//  Декаль крови (пятно на поверхности)
// ───────────────────────────────────────────────────────────────
struct BloodDecal {
    glm::vec3 pos;
    glm::vec3 normal;    // нормаль поверхности
    float     size;
    float     life;      // 0..1, уменьшается медленно
    float     maxLife;
    float     rotation;  // угол вращения квада
    bool      active = false;
};

// ───────────────────────────────────────────────────────────────
//  BloodFX
// ───────────────────────────────────────────────────────────────
struct BloodFX {
    // ── GPU ───────────────────────────────────────────────────
    unsigned int partShader = 0, decalShader = 0;
    unsigned int partVAO = 0, partVBO = 0;
    unsigned int decalVAO = 0, decalVBO = 0, decalEBO = 0;

    // ── Данные ────────────────────────────────────────────────
    BloodParticle particles[BLOOD_MAX_PARTICLES];
    BloodDecal    decals[BLOOD_MAX_DECALS];

    // CPU буферы для стриминга на GPU
    struct PVtx { glm::vec3 pos; float life; };
    std::vector<PVtx> partBuf;

    bool ready = false;

    // ── Утилиты ───────────────────────────────────────────────
    static float rnd(float lo, float hi) {
        return lo + (hi - lo) * ((float)(rand() % 1000) / 999.f);
    }
    static glm::vec3 rndDir() {
        float theta = rnd(0.f, 6.2831f);
        float phi = rnd(0.f, 3.1415f);
        return { sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta) };
    }

    // ── Компиляция шейдера ────────────────────────────────────
    unsigned int compileShader(const char* vs, const char* fs) {
        auto compile = [](unsigned int type, const char* src) -> unsigned int {
            unsigned int sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, NULL);
            glCompileShader(sh);
            int ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[512]; glGetShaderInfoLog(sh, 512, NULL, log);
                printf("[BLOOD] Shader error: %s\n", log);
            }
            return sh;
            };
        unsigned int v = compile(GL_VERTEX_SHADER, vs);
        unsigned int f = compile(GL_FRAGMENT_SHADER, fs);
        unsigned int p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f);
        return p;
    }

    // ── Инициализация ─────────────────────────────────────────
    void init() {
        partShader = compileShader(BLOOD_VERT, BLOOD_FRAG);
        decalShader = compileShader(DECAL_VERT, DECAL_FRAG);

        // Particle VAO
        glGenVertexArrays(1, &partVAO);
        glGenBuffers(1, &partVBO);
        glBindVertexArray(partVAO);
        glBindBuffer(GL_ARRAY_BUFFER, partVBO);
        glBufferData(GL_ARRAY_BUFFER, BLOOD_MAX_PARTICLES * sizeof(PVtx), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PVtx), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(PVtx), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        // Decal VAO (unit quad)
        float quadV[] = {
            -0.5f,0,-0.5f, 0,0,
             0.5f,0,-0.5f, 1,0,
             0.5f,0, 0.5f, 1,1,
            -0.5f,0, 0.5f, 0,1,
        };
        unsigned int qi[] = { 0,1,2, 0,2,3 };
        glGenVertexArrays(1, &decalVAO);
        glGenBuffers(1, &decalVBO);
        glGenBuffers(1, &decalEBO);
        glBindVertexArray(decalVAO);
        glBindBuffer(GL_ARRAY_BUFFER, decalVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadV), quadV, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, decalEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(qi), qi, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        partBuf.reserve(BLOOD_MAX_PARTICLES);
        ready = true;
        printf("[BLOOD] BloodFX initialized\n");
    }

    // ── Спавн частиц при попадании ────────────────────────────
    void spawnHit(const glm::vec3& pos, const glm::vec3& normal,
        const glm::vec3& shootDir, int count = 22)
    {
        // Брызги в конусе вокруг нормали (отлёт от поверхности)
        glm::vec3 reflect = glm::normalize(glm::reflect(shootDir, normal));

        for (int i = 0; i < count; i++) {
            BloodParticle* p = _allocParticle();
            if (!p) break;

            // Конус ~60° вокруг нормали
            glm::vec3 dir = glm::normalize(normal + rndDir() * 0.9f);
            float speed = rnd(2.5f, 9.f);

            p->pos = pos + normal * 0.02f;
            p->vel = dir * speed + reflect * rnd(0.5f, 2.f);
            p->maxLife = rnd(0.25f, BLOOD_PARTICLE_LIFE);
            p->life = p->maxLife;
            p->active = true;
        }

        // Пятно на поверхности
        _spawnDecal(pos, normal, rnd(0.12f, 0.28f));
    }

    // ── Спавн при смерти врага (много крови) ──────────────────
    void spawnDeath(const glm::vec3& pos, const glm::vec3& shootDir, int count = 60)
    {
        // Несколько взрывов в разных точках тела
        float offsets[][3] = {
            {0,1.7f,0}, {0,1.3f,0}, {0,1.0f,0}, {0,0.6f,0}
        };
        for (auto& o : offsets) {
            glm::vec3 bonePos = pos + glm::vec3(o[0], o[1], o[2]);
            glm::vec3 normal = glm::normalize(-shootDir + glm::vec3(0, 0.3f, 0));

            int c = count / 4 + (rand() % 5);
            for (int i = 0; i < c; i++) {
                BloodParticle* p = _allocParticle();
                if (!p) break;

                glm::vec3 dir = glm::normalize(rndDir() + normal * 0.5f);
                float speed = rnd(3.f, 14.f);

                p->pos = bonePos + rndDir() * 0.08f;
                p->vel = dir * speed;
                p->maxLife = rnd(0.3f, 0.7f);
                p->life = p->maxLife;
                p->active = true;
            }

            // Большое пятно на полу
            glm::vec3 floorPos = pos + glm::vec3(rnd(-0.15f, 0.15f), 0.01f, rnd(-0.15f, 0.15f));
            _spawnDecal(floorPos, { 0,1,0 }, rnd(0.18f, 0.42f));
        }
    }

    // ── Update ────────────────────────────────────────────────
    void update(float dt) {
        for (auto& p : particles) {
            if (!p.active) continue;
            p.life -= dt / p.maxLife;   // нормализованная жизнь 0..1
            if (p.life <= 0.f) { p.active = false; continue; }

            p.vel.y += BLOOD_GRAVITY * dt;
            // Сопротивление воздуха (кровь вязкая)
            p.vel *= (1.f - dt * 2.2f);
            p.pos += p.vel * dt;

            // Удар о пол — прилипает
            extern float getGroundY(const glm::vec3&, float);
            float gy = getGroundY(p.pos, 3.f);
            if (gy > -9000.f && p.pos.y < gy + 0.015f) {
                p.pos.y = gy + 0.01f;
                if (glm::length(p.vel) > 0.5f) {
                    // Маленькое пятнышко на полу при ударе
                    if (rand() % 4 == 0)
                        _spawnDecal(p.pos, { 0,1,0 }, rnd(0.04f, 0.1f));
                }
                p.vel = glm::vec3(0.f);
                p.life = std::min(p.life, 0.08f);  // быстро исчезает
            }
        }

        // Декали медленно тускнеют
        for (auto& d : decals) {
            if (!d.active) continue;
            d.life -= dt / d.maxLife;
            if (d.life <= 0.f) d.active = false;
        }
    }

    // ── Draw ──────────────────────────────────────────────────
    void draw(const glm::mat4& view, const glm::mat4& proj) {
        if (!ready) return;

        glm::mat4 VP = proj * view;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);  // частицы не пишут глубину

        // ── Частицы ──────────────────────────────────────────
        partBuf.clear();
        for (auto& p : particles) {
            if (!p.active) continue;
            partBuf.push_back({ p.pos, p.life });
        }

        if (!partBuf.empty()) {
            glEnable(GL_PROGRAM_POINT_SIZE);
            glUseProgram(partShader);
            glUniformMatrix4fv(glGetUniformLocation(partShader, "uVP"),
                1, GL_FALSE, glm::value_ptr(VP));
            glUniform1f(glGetUniformLocation(partShader, "uSize"), 9.f);

            glBindVertexArray(partVAO);
            glBindBuffer(GL_ARRAY_BUFFER, partVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                partBuf.size() * sizeof(PVtx), partBuf.data());
            glDrawArrays(GL_POINTS, 0, (int)partBuf.size());
            glBindVertexArray(0);
            glDisable(GL_PROGRAM_POINT_SIZE);
        }

        // ── Декали ───────────────────────────────────────────
        glUseProgram(decalShader);
        glBindVertexArray(decalVAO);
        // Небольшой polygon offset чтобы декали не z-файтились
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.f, -1.f);

        for (auto& d : decals) {
            if (!d.active) continue;

            // Строим матрицу ориентации квада по нормали
            glm::vec3 up = (fabsf(d.normal.y) < 0.99f)
                ? glm::vec3(0, 1, 0)
                : glm::vec3(1, 0, 0);
            glm::vec3 right = glm::normalize(glm::cross(up, d.normal));
            glm::vec3 fwd = glm::normalize(glm::cross(d.normal, right));

            glm::mat4 R = glm::mat4(1.f);
            R[0] = glm::vec4(right, 0);
            R[1] = glm::vec4(d.normal, 0);
            R[2] = glm::vec4(fwd, 0);
            R[3] = glm::vec4(0, 0, 0, 1);

            glm::mat4 model = glm::translate(glm::mat4(1.f), d.pos)
                * R
                * glm::rotate(glm::mat4(1.f), d.rotation, glm::vec3(0, 1, 0))
                * glm::scale(glm::mat4(1.f), glm::vec3(d.size));

            glUniformMatrix4fv(glGetUniformLocation(decalShader, "uMVP"),
                1, GL_FALSE, glm::value_ptr(VP * model));

            // alpha: плавное появление + тусклое исчезание
            float fadeIn = std::min(1.f, (1.f - d.life) * 8.f + d.life);
            float fadeOut = (d.life < 0.15f) ? d.life / 0.15f : 1.f;
            glUniform1f(glGetUniformLocation(decalShader, "uAlpha"),
                fadeIn * fadeOut * 0.88f);

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }

        glBindVertexArray(0);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // ── Cleanup ───────────────────────────────────────────────
    void shutdown() {
        if (partVAO) { glDeleteVertexArrays(1, &partVAO); partVAO = 0; }
        if (partVBO) { glDeleteBuffers(1, &partVBO); partVBO = 0; }
        if (decalVAO) { glDeleteVertexArrays(1, &decalVAO); decalVAO = 0; }
        if (decalVBO) { glDeleteBuffers(1, &decalVBO); decalVBO = 0; }
        if (decalEBO) { glDeleteBuffers(1, &decalEBO); decalEBO = 0; }
        if (partShader) { glDeleteProgram(partShader); partShader = 0; }
        if (decalShader) { glDeleteProgram(decalShader); decalShader = 0; }
    }

private:
    BloodParticle* _allocParticle() {
        // Ищем свободную или самую старую
        BloodParticle* oldest = nullptr;
        float minLife = 2.f;
        for (auto& p : particles) {
            if (!p.active) return &p;
            if (p.life < minLife) { minLife = p.life; oldest = &p; }
        }
        return oldest; // переиспользуем самую старую
    }

    void _spawnDecal(const glm::vec3& pos, const glm::vec3& normal, float size) {
        BloodDecal* oldest = nullptr;
        float minLife = 2.f;
        for (auto& d : decals) {
            if (!d.active) { oldest = &d; break; }
            if (d.life < minLife) { minLife = d.life; oldest = &d; }
        }
        if (!oldest) return;
        oldest->pos = pos;
        oldest->normal = normal;
        oldest->size = size;
        oldest->maxLife = BLOOD_DECAL_LIFE;
        oldest->life = 1.f;
        oldest->rotation = rnd(0.f, 6.28f);
        oldest->active = true;
    }
};

inline BloodFX bloodFX;