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
#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include "Settings.h"

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


// ── HLSL шейдеры BloodFX (DX11) ─────────────────────────────
#ifdef _WIN32
static const char* HLSL_BLOOD_PARTICLE = R"(
cbuffer CB:register(b0){matrix uVP;float uSize;float3 _p;};
struct VI{float3 pos:POSITION;float life:TEXCOORD0;};
struct V2P{float4 pos:SV_Position;float life:TEXCOORD0;float2 uv:TEXCOORD1;float sz:TEXCOORD2;};
V2P VSMain(VI i){
    V2P o;
    o.pos=mul(uVP,float4(i.pos,1));
    o.life=i.life;
    float dist=max(o.pos.z,0.01);
    o.sz=clamp(uSize*(0.4+i.life*0.6)*(1.0/(1.0+dist*0.08)),1,18);
    o.uv=float2(0,0);
    return o;
}
// GS разворачивает точку в билборд-квад
struct GS_OUT{float4 pos:SV_Position;float life:TEXCOORD0;float2 uv:TEXCOORD1;};
[maxvertexcount(4)]
void GSMain(point V2P gin[1],inout TriangleStream<GS_OUT> s){
    float2 off=gin[0].sz*0.5/float2(1280,720);
    float4 p=gin[0].pos/gin[0].pos.w;
    float2 corners[4]={float2(-1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    float2 uvs[4]={float2(0,1),float2(1,1),float2(0,0),float2(1,0)};
    [unroll]for(int i=0;i<4;i++){
        GS_OUT o;
        o.pos=float4(p.xy+corners[i]*off,p.z,1);
        o.life=gin[0].life;
        o.uv=uvs[i];
        s.Append(o);
    }
}
float4 PSMain(GS_OUT i):SV_Target{
    float2 c=i.uv*2-1;
    float d=dot(c,c);
    if(d>1)discard;
    float alpha=i.life*i.life*(1-d*0.4);
    float3 col=lerp(float3(0.10,0,0),float3(0.65,0.03,0.03),i.life);
    return float4(col,alpha*0.92);
}
)";
static const char* HLSL_BLOOD_DECAL = R"(
cbuffer CB:register(b0){matrix uMVP;float uAlpha;float3 _p;};
struct VI{float3 pos:POSITION;float2 uv:TEXCOORD0;};
struct V2P{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V2P VSMain(VI i){V2P o;o.pos=mul(uMVP,float4(i.pos,1));o.uv=i.uv;return o;}
float4 PSMain(V2P i):SV_Target{
    float2 c=i.uv*2-1;
    float d=length(c);
    float angle=atan2(c.y,c.x);
    float jagged=0.08*sin(angle*7)+0.05*sin(angle*13+1.2);
    float shape=smoothstep(0.85+jagged,0.60+jagged,d);
    float inner=smoothstep(0.5,0.0,d)*0.35;
    float alpha=(shape+inner)*uAlpha;
    if(alpha<0.02)discard;
    float3 col=float3(0.25,0,0)+float3(0.1,0,0)*inner;
    return float4(col,alpha);
}
)";
#endif
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
    // ── GPU OpenGL ────────────────────────────────────────────
    unsigned int partShader = 0, decalShader = 0;
    unsigned int partVAO = 0, partVBO = 0;
    unsigned int decalVAO = 0, decalVBO = 0, decalEBO = 0;
#ifdef _WIN32
    // ── GPU DX11 ─────────────────────────────────────────────
    ComPtr<ID3D11Device>        dxDev;
    ComPtr<ID3D11DeviceContext> dxCtx;
    ComPtr<ID3D11VertexShader>  dxPartVS;
    ComPtr<ID3D11GeometryShader>dxPartGS;
    ComPtr<ID3D11PixelShader>   dxPartPS;
    ComPtr<ID3D11VertexShader>  dxDecalVS;
    ComPtr<ID3D11PixelShader>   dxDecalPS;
    ComPtr<ID3D11InputLayout>   dxPartIL, dxDecalIL;
    ComPtr<ID3D11Buffer>        dxPartVB, dxDecalVB, dxDecalIB;
    ComPtr<ID3D11Buffer>        dxPartCB, dxDecalCB;
    ComPtr<ID3D11BlendState>    dxBS;
    ComPtr<ID3D11DepthStencilState> dxDSS;
    ComPtr<ID3D11RasterizerState>   dxRS;
    bool dxReady = false;
#endif

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
    // Для DX11: передай device + context, они не null → DX11 путь
    // Для OpenGL: вызывай без аргументов или с nullptr
    void init(void* dev_ = nullptr, void* ctx_ = nullptr) {
#ifdef _WIN32
        if (dev_ && ctx_) {
            dxDev = static_cast<ID3D11Device*>(dev_);
            dxCtx = static_cast<ID3D11DeviceContext*>(ctx_);
            _dxInit();
            ready = true;
            printf("[BLOOD] BloodFX DX11 initialized\n");
            return;
        }
#endif
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
#ifdef _WIN32
        if (dxReady) { _dxDraw(view, proj); return; }
#endif

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
#ifdef _WIN32
        if (dxReady) { dxReady = false; return; } // ComPtr освободит сами
#endif
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

#ifdef _WIN32
    // ── DX11 Init ─────────────────────────────────────────────
    void _dxInit() {
        // Компилируем шейдеры частиц (VS + GS + PS)
        auto compile = [&](const char* src, const char* entry, const char* target, ComPtr<ID3DBlob>& blob) -> bool {
            ComPtr<ID3DBlob> err;
            HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &blob, &err);
            if (FAILED(hr)) { if (err) printf("[BLOOD] %s: %s\n", entry, (char*)err->GetBufferPointer()); return false; }
            return true;
            };
        ComPtr<ID3DBlob> vsb, gsb, psb, vsb2, psb2;
        if (!compile(HLSL_BLOOD_PARTICLE, "VSMain", "vs_5_0", vsb)) return;
        if (!compile(HLSL_BLOOD_PARTICLE, "GSMain", "gs_5_0", gsb)) return;
        if (!compile(HLSL_BLOOD_PARTICLE, "PSMain", "ps_5_0", psb)) return;
        if (!compile(HLSL_BLOOD_DECAL, "VSMain", "vs_5_0", vsb2)) return;
        if (!compile(HLSL_BLOOD_DECAL, "PSMain", "ps_5_0", psb2)) return;

        dxDev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &dxPartVS);
        dxDev->CreateGeometryShader(gsb->GetBufferPointer(), gsb->GetBufferSize(), nullptr, &dxPartGS);
        dxDev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &dxPartPS);
        dxDev->CreateVertexShader(vsb2->GetBufferPointer(), vsb2->GetBufferSize(), nullptr, &dxDecalVS);
        dxDev->CreatePixelShader(psb2->GetBufferPointer(), psb2->GetBufferSize(), nullptr, &dxDecalPS);

        // Input layouts
        D3D11_INPUT_ELEMENT_DESC partLay[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32_FLOAT,       0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        D3D11_INPUT_ELEMENT_DESC decalLay[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,   0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        dxDev->CreateInputLayout(partLay, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &dxPartIL);
        dxDev->CreateInputLayout(decalLay, 2, vsb2->GetBufferPointer(), vsb2->GetBufferSize(), &dxDecalIL);

        // Vertex buffers
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = BLOOD_MAX_PARTICLES * sizeof(PVtx);
            bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            dxDev->CreateBuffer(&bd, nullptr, &dxPartVB);
        }
        // Decal quad VB (unit quad, 4 verts, xz plane)
        {
            float qv[] = {
                -0.5f,0,-0.5f, 0,0,
                 0.5f,0,-0.5f, 1,0,
                 0.5f,0, 0.5f, 1,1,
                -0.5f,0, 0.5f, 0,1,
            };
            D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(qv);
            bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = qv;
            dxDev->CreateBuffer(&bd, &sd, &dxDecalVB);
            unsigned int qi[] = { 0,1,2,0,2,3 };
            bd.ByteWidth = sizeof(qi); bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.Usage = D3D11_USAGE_IMMUTABLE;
            sd.pSysMem = qi;
            dxDev->CreateBuffer(&bd, &sd, &dxDecalIB);
        }
        // Constant buffers
        {
            D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bd.ByteWidth = 32; dxDev->CreateBuffer(&bd, nullptr, &dxPartCB);  // mat4+float+pad
            bd.ByteWidth = 32; dxDev->CreateBuffer(&bd, nullptr, &dxDecalCB);
        }
        // Blend state: src_alpha / one_minus_src_alpha
        {
            D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            dxDev->CreateBlendState(&bd, &dxBS);
        }
        // DSS: depth test ON, depth write OFF (частицы не пишут глубину)
        {
            D3D11_DEPTH_STENCIL_DESC dd = {};
            dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; dd.StencilEnable = FALSE;
            dxDev->CreateDepthStencilState(&dd, &dxDSS);
        }
        // RS: no cull, polygon offset для декалей
        {
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE; rd.SlopeScaledDepthBias = -1.f;
            dxDev->CreateRasterizerState(&rd, &dxRS);
        }
        dxReady = true;
    }

    struct alignas(16) DX_BloodPartCB { float vp[16]; float size; float pad[3]; };
    struct alignas(16) DX_BloodDecalCB { float mvp[16]; float alpha; float pad[3]; };

    // ── DX11 Draw ─────────────────────────────────────────────
    void _dxDraw(const glm::mat4& view, const glm::mat4& proj) {
        glm::mat4 VP = proj * view;

        dxCtx->RSSetState(dxRS.Get());
        dxCtx->OMSetDepthStencilState(dxDSS.Get(), 0);
        float bf[4] = { 0,0,0,0 };
        dxCtx->OMSetBlendState(dxBS.Get(), bf, 0xFFFFFFFF);

        // ── Частицы ──────────────────────────────────────────
        partBuf.clear();
        for (auto& p : particles)
            if (p.active) partBuf.push_back({ p.pos, p.life });

        if (!partBuf.empty()) {
            // Upload verts
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(dxCtx->Map(dxPartVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                size_t bytes = partBuf.size() * sizeof(PVtx);
                memcpy(ms.pData, partBuf.data(), bytes);
                dxCtx->Unmap(dxPartVB.Get(), 0);
            }
            // CB
            DX_BloodPartCB cb; memcpy(cb.vp, glm::value_ptr(VP), 64); cb.size = 9.f;
            if (SUCCEEDED(dxCtx->Map(dxPartCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                memcpy(ms.pData, &cb, sizeof(cb)); dxCtx->Unmap(dxPartCB.Get(), 0);
            }
            dxCtx->VSSetShader(dxPartVS.Get(), nullptr, 0);
            dxCtx->GSSetShader(dxPartGS.Get(), nullptr, 0);
            dxCtx->PSSetShader(dxPartPS.Get(), nullptr, 0);
            dxCtx->IASetInputLayout(dxPartIL.Get());
            dxCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            dxCtx->VSSetConstantBuffers(0, 1, dxPartCB.GetAddressOf());
            dxCtx->GSSetConstantBuffers(0, 1, dxPartCB.GetAddressOf());
            UINT st = sizeof(PVtx), off = 0;
            dxCtx->IASetVertexBuffers(0, 1, dxPartVB.GetAddressOf(), &st, &off);
            dxCtx->Draw((UINT)partBuf.size(), 0);
            dxCtx->GSSetShader(nullptr, nullptr, 0);  // выключаем GS после
        }

        // ── Декали ───────────────────────────────────────────
        dxCtx->VSSetShader(dxDecalVS.Get(), nullptr, 0);
        dxCtx->PSSetShader(dxDecalPS.Get(), nullptr, 0);
        dxCtx->IASetInputLayout(dxDecalIL.Get());
        dxCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT dst = sizeof(float) * 5, doff = 0;
        dxCtx->IASetVertexBuffers(0, 1, dxDecalVB.GetAddressOf(), &dst, &doff);
        dxCtx->IASetIndexBuffer(dxDecalIB.Get(), DXGI_FORMAT_R32_UINT, 0);

        for (auto& d : decals) {
            if (!d.active) continue;
            // Матрица декали
            glm::vec3 up = (fabsf(d.normal.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 right = glm::normalize(glm::cross(up, d.normal));
            glm::vec3 fwd = glm::normalize(glm::cross(d.normal, right));
            glm::mat4 R(1.f); R[0] = glm::vec4(right, 0); R[1] = glm::vec4(d.normal, 0); R[2] = glm::vec4(fwd, 0);
            glm::mat4 model = glm::translate(glm::mat4(1.f), d.pos) * R
                * glm::rotate(glm::mat4(1.f), d.rotation, glm::vec3(0, 1, 0))
                * glm::scale(glm::mat4(1.f), glm::vec3(d.size));
            glm::mat4 mvp = VP * model;
            float fadeIn = std::min(1.f, (1.f - d.life) * 8.f + d.life);
            float fadeOut = (d.life < 0.15f) ? d.life / 0.15f : 1.f;
            float alpha = fadeIn * fadeOut * 0.88f;

            D3D11_MAPPED_SUBRESOURCE ms;
            DX_BloodDecalCB cb; memcpy(cb.mvp, glm::value_ptr(mvp), 64); cb.alpha = alpha;
            if (SUCCEEDED(dxCtx->Map(dxDecalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                memcpy(ms.pData, &cb, sizeof(cb)); dxCtx->Unmap(dxDecalCB.Get(), 0);
            }
            dxCtx->VSSetConstantBuffers(0, 1, dxDecalCB.GetAddressOf());
            dxCtx->PSSetConstantBuffers(0, 1, dxDecalCB.GetAddressOf());
            dxCtx->DrawIndexed(6, 0, 0);
        }

        // Восстанавливаем состояния
        dxCtx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        dxCtx->OMSetDepthStencilState(nullptr, 0);
    }
#endif
};

inline BloodFX bloodFX;