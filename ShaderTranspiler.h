#pragma once
// ================================================================
//  ShaderTranspiler.h  —  Universal shader system
//
//  АРХИТЕКТУРА: один canonical source → все API
//
//  Сейчас:
//    OpenGL 3.3  → нативный GLSL 330 (прямая компиляция, без SPIR-V)
//    DX11        → HLSL компилируется D3DCompile (в Renderer.h)
//
//  Будущее (API уже готов):
//    Vulkan      → vsSpirv() / psSpirv() → vkCreateShaderModule
//    DX12        → vsSpirv() → D3D12CreateShaderResourceView
//    Metal       → spirv-cross MSL (добавить _spirvToMsl())
//
//  ПОЧЕМУ НЕ SPIR-V ДЛЯ OPENGL 3.3:
//    spirv-cross генерирует uniform struct с числовыми именами:
//      uniform PerFrame _64;   ← glGetUniformLocation("view") = -1!
//    И row-major матрицы транспонируются неправильно.
//    GL 4.6 + GL_ARB_gl_spirv умеет бинарный SPIR-V напрямую —
//    но 3.3 не умеет. Поэтому для 3.3 — нативный GLSL.
//    Для Vulkan/DX12 SPIR-V путь работает идеально.
//
//  КАК БЫЛ СЛОМАН ПРЕДЫДУЩИЙ КОД (чёрный экран):
//    setEnvClient(EShClientOpenGL, ...)  ← не понимает HLSL cbuffer
//    parse() падал → SPIR-V пустой → GLSL пустой → shader=0
//
//  ЗАВИСИМОСТИ:
//    Для OpenGL пути: зависимостей нет (чистый GLSL)
//    Для Vulkan пути: vcpkg install glslang spirv-cross
// ================================================================

// ── Vulkan-path зависимости (подключаются только если нужен SPIR-V) ──
#ifdef SHADER_TRANSPILER_SPIRV
#  include <glslang/Public/ShaderLang.h>
#  include <glslang/SPIRV/GlslangToSpv.h>
#  include <glslang/Public/ResourceLimits.h>
#  include <spirv_cross/spirv_glsl.hpp>
#  include <spirv_cross/spirv_hlsl.hpp>
#endif

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cstdio>

namespace ShaderTranspiler {

    inline void init() { /* no-op для GL пути */ }
    inline void shutdown() {}

    // ================================================================
    //  GLSL 330 ШЕЙДЕРЫ
    //  Логика 1-в-1 с HLSL шейдерами из Renderer.h
    //  Имена uniform совпадают с glGetUniformLocation вызовами
    // ================================================================

    // ── WORLD SHADER (HLSL_WORLD) ────────────────────────────────
    static const char* GLSL_WORLD_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec3 n;
layout(location=2) in vec2 uv;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;
uniform mat4 lightSpaceMatrix;
uniform vec3 lightDir;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 camPos;

out vec3 _norm;
out vec2 _uv;
out vec3 _wp;
out float _fd;
out vec4 _lsp;

void main(){
    vec4 wp = model * vec4(p, 1.0);
    _wp  = wp.xyz;
    _norm = mat3(normalMatrix) * n;
    _uv  = uv;
    _lsp = lightSpaceMatrix * wp;
    vec4 vp = view * wp;
    _fd  = -vp.z;
    gl_Position = projection * vp;
}
)GLSL";

    static const char* GLSL_WORLD_FRAG = R"GLSL(
#version 330 core
in vec3 _norm;
in vec2 _uv;
in vec3 _wp;
in float _fd;
in vec4 _lsp;

uniform sampler2D tex;
uniform sampler2D shadowMap;
uniform vec3  lightDir;
uniform vec3  fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3  baseColor;
uniform int   hasTexture;

out vec4 fragColor;

vec3 ACES(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }
vec3 sat3(vec3 c, float s){
    float l = dot(c, vec3(0.2126,0.7152,0.0722));
    return mix(vec3(l), c, s);
}

float shadowPCF(vec4 lsp, float NdL){
    vec3 prj = lsp.xyz / lsp.w;
    prj = prj * 0.5 + 0.5;
    prj.y = 1.0 - prj.y;
    if(prj.z > 1.0) return 0.0;
    if(any(lessThan(prj.xy, vec2(0.0))) || any(greaterThan(prj.xy, vec2(1.0)))) return 0.0;
    float bias = max(0.005*(1.0-NdL), 0.002);
    vec2 ts = 1.0 / vec2(textureSize(shadowMap, 0));
    float s = 0.0;
    for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++){
        float d = texture(shadowMap, prj.xy + vec2(x,y)*ts).r;
        s += (prj.z - bias > d) ? 1.0 : 0.0;
    }
    return (s/9.0)*0.65;
}

void main(){
    vec3 alb = (hasTexture != 0)
        ? pow(texture(tex, _uv).rgb, vec3(2.2))
        : baseColor;
    vec3 N = normalize(_norm);
    vec3 L = normalize(-lightDir);
    float NdL = max(dot(N, L), 0.0);
    float shadow = shadowPCF(_lsp, NdL);

    vec3 lit = alb * (
        vec3(0.25,0.22,0.20)
        + vec3(1.05,0.95,0.80)*NdL*0.85*(1.0-shadow)
        + vec3(0.55,0.70,0.90)*max(dot(N,vec3(0,1,0)),0.0)*0.25
        + vec3(0.40,0.35,0.28)*max(dot(N,vec3(0,-1,0)),0.0)*0.12
    );
    lit += pow(max(dot(normalize(L+vec3(0,0,1)),N),0.0),32.0)*0.15*(1.0-shadow);
    lit = sat3(lit, 1.2);
    lit = ACES(lit * 0.8);
    float fogT = clamp((_fd-fogStart)/(fogEnd-fogStart),0.0,1.0);
    fogT = fogT*fogT*fogT;
    lit = mix(lit, fogColor, fogT);
    fragColor = vec4(lit, 1.0);
}
)GLSL";

    // ── GUN / ENEMY SHADER (HLSL_GUN) ───────────────────────────
    static const char* GLSL_GUN_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec3 n;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 blendIdx;
layout(location=4) in vec4 blendWt;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;
uniform int  skinned;
uniform mat4 bones[100];

out vec3 _norm;
out vec2 _uv;

void main(){
    vec4 pos = vec4(p, 1.0);
    vec3 nor = n;
    if(skinned != 0){
        float ws = blendWt.x+blendWt.y+blendWt.z+blendWt.w;
        mat4 skin = mat4(0.0);
        if(ws < 0.001) skin = mat4(1.0);
        else for(int k=0;k<4;k++){
            int id = int(blendIdx[k]);
            if(id>=0 && id<100 && blendWt[k]>0.0)
                skin += bones[id]*blendWt[k];
        }
        pos = skin * pos;
        nor = mat3(skin) * nor;
    }
    _norm = mat3(normalMatrix) * nor;
    _uv   = uv;
    gl_Position = projection * view * model * pos;
}
)GLSL";

    static const char* GLSL_GUN_FRAG = R"GLSL(
#version 330 core
in vec3 _norm;
in vec2 _uv;

uniform sampler2D tex;
uniform vec3  gunColor;
uniform float flash;
uniform int   hasTexture;

out vec4 fragColor;

vec3 ACES(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }

void main(){
    vec3 alb = (hasTexture != 0)
        ? pow(texture(tex, _uv).rgb, vec3(2.2))
        : gunColor;
    vec3 N = normalize(_norm);
    float d1  = max(dot(N, normalize(vec3(0.3,0.8,0.5))), 0.0);
    float d2  = max(dot(N, normalize(vec3(-0.5,0.3,-0.8))), 0.0)*0.25;
    float rim = pow(1.0-max(dot(N,vec3(0,0,1)),0.0),3.0)*0.15;
    vec3  H   = normalize(normalize(vec3(0.3,0.8,0.5))+vec3(0,0,1));
    float sp  = pow(max(dot(H,N),0.0),64.0)*0.4;
    vec3 lit  = alb*(0.18+d1*0.55+d2+rim) + vec3(sp*0.6)
              + vec3(1.0,0.85,0.5)*flash*0.3;
    lit = ACES(lit);
    fragColor = vec4(lit, 1.0);
}
)GLSL";

    // ── POST-PROCESS SHADER (HLSL_POST) ─────────────────────────
    // Quad вершины передаются через VAO (xy + uv), не SV_VertexID
    static const char* GLSL_POST_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 _uv;
void main(){ _uv = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

    static const char* GLSL_POST_FRAG = R"GLSL(
#version 330 core
in vec2 _uv;
uniform sampler2D screenTex;
uniform vec2  resolution;
uniform float time;
uniform float hp01;
out vec4 fragColor;

void main(){
    vec2 uv = _uv;
    vec2 px = 1.0/resolution;
    vec3 col = texture(screenTex, uv).rgb;

    // Sharpening
    vec3 sh = col*5.0
        - texture(screenTex, uv+vec2(-1,0)*px).rgb
        - texture(screenTex, uv+vec2( 1,0)*px).rgb
        - texture(screenTex, uv+vec2( 0,-1)*px).rgb
        - texture(screenTex, uv+vec2( 0, 1)*px).rgb;
    col = mix(col, sh, 0.18);

    // Soft contrast
    col = col*col*(3.0-2.0*col);

    // Cold shadows / warm highlights
    float lum = dot(col, vec3(0.2126,0.7152,0.0722));
    col = mix(col*vec3(0.78,0.85,1.0), col*vec3(1.0,0.97,0.90),
              clamp(lum*2.0,0.0,1.0));

    // Slight desaturate
    float grey = dot(col, vec3(0.299,0.587,0.114));
    col = mix(vec3(grey), col, 0.85);

    // Gamma encode
    col = pow(max(col,vec3(0.0)), vec3(1.0/2.2));

    // Vignette
    float d = length(uv-0.5);
    col *= 1.0 - d*d*0.75;

    // Low HP damage pulse
    if(hp01 < 0.30){
        float p2    = sin(time*2.5)*0.5+0.5;
        float inten = (0.30-hp01)/0.30*0.50*(0.5+0.5*p2);
        float edge  = smoothstep(0.20,0.50,d);
        col = mix(col, vec3(col.r*0.5,0.0,0.0), inten*edge);
    }

    // Film grain
    float noise = fract(sin(dot(uv+fract(vec2(time)),
                   vec2(127.1,311.7)))*43758.5453);
    col += (noise-0.5)*0.012;

    fragColor = vec4(clamp(col,0.0,1.0), 1.0);
}
)GLSL";

    // ── SHADOW MAP SHADER (HLSL_SHADOW) ─────────────────────────
    static const char* GLSL_SHADOW_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec3 n;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 blendIdx;
layout(location=4) in vec4 blendWt;

uniform mat4 lightMVP;
uniform int  skinned;
uniform mat4 bones[100];

void main(){
    vec4 pos = vec4(p,1.0);
    if(skinned!=0){
        float ws = blendWt.x+blendWt.y+blendWt.z+blendWt.w;
        mat4 skin = mat4(0.0);
        if(ws<0.001) skin=mat4(1.0);
        else for(int k=0;k<4;k++){
            int id=int(blendIdx[k]);
            if(id>=0&&id<100&&blendWt[k]>0.0) skin+=bones[id]*blendWt[k];
        }
        pos=skin*pos;
    }
    gl_Position = lightMVP*pos;
}
)GLSL";

    static const char* GLSL_SHADOW_FRAG = R"GLSL(
#version 330 core
void main(){}
)GLSL";

    // ── DOT / BULLET-HOLE SHADER (HLSL_DOT) ─────────────────────
    static const char* GLSL_DOT_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec3 p;
uniform mat4 mvp;
void main(){ gl_Position = mvp*vec4(p,1.0); }
)GLSL";

    static const char* GLSL_DOT_FRAG = R"GLSL(
#version 330 core
uniform vec4 color;
out vec4 fragColor;
void main(){ fragColor = color; }
)GLSL";

    // ================================================================
    //  ОПРЕДЕЛЕНИЕ ТИПА ШЕЙДЕРА
    //  Ищем уникальные строки в HLSL исходнике
    // ================================================================
    enum class Kind { WORLD, GUN, POST, SHADOW, DOT, UNKNOWN };

    static Kind detect(const char* hlsl) {
        if (!hlsl) return Kind::UNKNOWN;
        // Порядок важен — от наиболее специфичного к общему
        if (strstr(hlsl, "lightSpaceMatrix") && strstr(hlsl, "shadowMap"))
            return Kind::WORLD;
        if (strstr(hlsl, "lightMVP"))
            return Kind::SHADOW;
        if (strstr(hlsl, "screenTex") || strstr(hlsl, "screen:register"))
            return Kind::POST;
        if (strstr(hlsl, "gunColor") || strstr(hlsl, "BLENDINDICES")
            || strstr(hlsl, "skinned"))
            return Kind::GUN;
        if (strstr(hlsl, "mvp") && strstr(hlsl, "color"))
            return Kind::DOT;
        return Kind::UNKNOWN;
    }

    // ================================================================
    //  ПУБЛИЧНЫЙ API
    //  Вызывается из Renderer.h: _glBuildHLSL(HLSL_WORLD) и т.д.
    // ================================================================

    // OpenGL: HLSL-строка → GLSL vertex shader
    inline std::string vsFromHLSL(const char* hlsl) {
        switch (detect(hlsl)) {
        case Kind::WORLD:  return GLSL_WORLD_VERT;
        case Kind::GUN:    return GLSL_GUN_VERT;
        case Kind::POST:   return GLSL_POST_VERT;
        case Kind::SHADOW: return GLSL_SHADOW_VERT;
        case Kind::DOT:    return GLSL_DOT_VERT;
        default:
            printf("[ShaderTranspiler] vsFromHLSL: unknown shader!\n");
            return "";
        }
    }

    // OpenGL: HLSL-строка → GLSL fragment shader
    inline std::string psFromHLSL(const char* hlsl) {
        switch (detect(hlsl)) {
        case Kind::WORLD:  return GLSL_WORLD_FRAG;
        case Kind::GUN:    return GLSL_GUN_FRAG;
        case Kind::POST:   return GLSL_POST_FRAG;
        case Kind::SHADOW: return GLSL_SHADOW_FRAG;
        case Kind::DOT:    return GLSL_DOT_FRAG;
        default:
            printf("[ShaderTranspiler] psFromHLSL: unknown shader!\n");
            return "";
        }
    }

#ifdef SHADER_TRANSPILER_SPIRV
    // ── Vulkan: HLSL → SPIR-V binary (грузи в vkCreateShaderModule) ──
    static std::vector<uint32_t> _toSpirv(const char* hlsl, EShLanguage stage,
        const char* ep)
    {
        glslang::TShader sh(stage);
        const char* src[] = { hlsl };
        sh.setStrings(src, 1);
        sh.setEnvInput(glslang::EShSourceHlsl, stage,
            glslang::EShClientVulkan, 130);
        sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        sh.setEntryPoint(ep);
        sh.setSourceEntryPoint(ep);
        sh.setAutoMapBindings(true);
        sh.setAutoMapLocations(true);
        auto msg = (EShMessages)(EShMsgSpvRules | EShMsgReadHlsl |
            EShMsgHlslOffsets | EShMsgHlslLegalization);
        if (!sh.parse(GetDefaultResources(), 130, false, msg)) {
            printf("[ST:SPIRV] Parse failed: %s\n", sh.getInfoLog());
            return {};
        }
        glslang::TProgram prog;
        prog.addShader(&sh);
        if (!prog.link(msg)) {
            printf("[ST:SPIRV] Link failed: %s\n", prog.getInfoLog());
            return {};
        }
        std::vector<uint32_t> spirv;
        spv::SpvBuildLogger log;
        glslang::SpvOptions opt; opt.optimizeSize = true;
        glslang::GlslangToSpv(*prog.getIntermediate(stage), spirv, &log, &opt);
        return spirv;
    }

    // Для Vulkan — получи SPIR-V и грузи напрямую
    inline std::vector<uint32_t> vsSpirv(const char* hlsl)
    {
        return _toSpirv(hlsl, EShLangVertex, "VSMain");
    }
    inline std::vector<uint32_t> psSpirv(const char* hlsl)
    {
        return _toSpirv(hlsl, EShLangFragment, "PSMain");
    }
#endif // SHADER_TRANSPILER_SPIRV

    // Очистка кэша (hot-reload)
    inline void clearCache() {
        printf("[ShaderTranspiler] Cache cleared (GL path has no cache)\n");
    }

} // namespace ShaderTranspiler