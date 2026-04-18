// ================================================================
//  ShaderTranspiler.cpp
//
//  Содержит:
//    - все GLSL шейдеры (для OpenGL)
//    - все HLSL шейдеры (для DX11)
//    - компилятор и линкер GL программ
//    - детектор типа шейдера по HLSL исходнику
//
//  Компилируется ОДИН РАЗ — не попадает в каждую единицу
//  трансляции как было бы при #pragma once + inline в .h
// ================================================================

#include "ShaderTranspiler.h"
#include <cstdio>
#include <cstring>

// ================================================================
//  GLSL 330 ШЕЙДЕРЫ  (OpenGL путь)
//  Та же логика что и HLSL ниже — одно освещение, одни тени.
// ================================================================

// ── World ────────────────────────────────────────────────────────
static const char* s_WorldVert = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;
uniform mat4 lightSpaceMatrix;

out vec3 vNorm;
out vec2 vUV;
out vec3 vWorldPos;
out float vFogDepth;
out vec4 vLightSpacePos;

void main(){
    vec4 worldPos  = model * vec4(aPos, 1.0);
    vWorldPos      = worldPos.xyz;
    vNorm          = mat3(normalMatrix) * aNorm;
    vUV            = aUV;
    vLightSpacePos = lightSpaceMatrix * worldPos;
    vec4 viewPos   = view * worldPos;
    vFogDepth      = -viewPos.z;
    gl_Position    = projection * viewPos;
}
)GLSL";

static const char* s_WorldFrag = R"GLSL(
#version 330 core
in vec3  vNorm;
in vec2  vUV;
in vec3  vWorldPos;
in float vFogDepth;
in vec4  vLightSpacePos;

uniform sampler2D tex;
uniform sampler2D shadowMap;
uniform vec3  lightDir;
uniform vec3  fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3  baseColor;
uniform int   hasTexture;

out vec4 fragColor;

vec3 ACES(vec3 x){
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}

float shadowPCF(vec4 lsp, float NdL){
    vec3 p = lsp.xyz / lsp.w;
    p = p * 0.5 + 0.5;
    p.y = 1.0 - p.y;
    if(p.z > 1.0) return 0.0;
    if(any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0)))) return 0.0;
    float bias = max(0.005*(1.0-NdL), 0.002);
    vec2  ts   = 1.0 / vec2(textureSize(shadowMap, 0));
    float s    = 0.0;
    for(int x=-1; x<=1; x++)
        for(int y=-1; y<=1; y++){
            float d = texture(shadowMap, p.xy + vec2(x,y)*ts).r;
            s += (p.z - bias > d) ? 1.0 : 0.0;
        }
    return (s / 9.0) * 0.65;
}

void main(){
    vec3 alb = (hasTexture != 0)
        ? pow(texture(tex, vUV).rgb, vec3(2.2))
        : baseColor;

    vec3  N      = normalize(vNorm);
    vec3  L      = normalize(-lightDir);
    float NdL    = max(dot(N, L), 0.0);
    float shadow = shadowPCF(vLightSpacePos, NdL);

    vec3 lit = alb * (
        vec3(0.25, 0.22, 0.20)
        + vec3(1.05, 0.95, 0.80) * NdL * 0.85 * (1.0-shadow)
        + vec3(0.55, 0.70, 0.90) * max(dot(N, vec3(0,1,0)), 0.0) * 0.25
        + vec3(0.40, 0.35, 0.28) * max(dot(N, vec3(0,-1,0)), 0.0) * 0.12
    );
    lit += pow(max(dot(normalize(L + vec3(0,0,1)), N), 0.0), 32.0) * 0.15 * (1.0-shadow);

    float grey = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(grey), lit, 1.2);
    lit = ACES(lit * 0.8);

    float fogT = clamp((vFogDepth - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogT = fogT*fogT*fogT;
    lit  = mix(lit, fogColor, fogT);

    fragColor = vec4(lit, 1.0);
}
)GLSL";

// ── Gun / Enemy ───────────────────────────────────────────────────
static const char* s_GunVert = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aBlendIdx;
layout(location=4) in vec4 aBlendWt;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;
uniform int  skinned;
uniform mat4 bones[100];

out vec3 vNorm;
out vec2 vUV;

void main(){
    vec4 pos = vec4(aPos, 1.0);
    vec3 nor = aNorm;

    if(skinned != 0){
        float ws = aBlendWt.x + aBlendWt.y + aBlendWt.z + aBlendWt.w;
        mat4  sk = mat4(0.0);
        if(ws < 0.001) sk = mat4(1.0);
        else for(int k=0; k<4; k++){
            int id = int(aBlendIdx[k]);
            if(id >= 0 && id < 100 && aBlendWt[k] > 0.0)
                sk += bones[id] * aBlendWt[k];
        }
        pos = sk * pos;
        nor = mat3(sk) * nor;
    }

    vNorm       = mat3(normalMatrix) * nor;
    vUV         = aUV;
    gl_Position = projection * view * model * pos;
}
)GLSL";

static const char* s_GunFrag = R"GLSL(
#version 330 core
in vec3 vNorm;
in vec2 vUV;

uniform sampler2D tex;
uniform vec3  gunColor;
uniform float flash;
uniform int   hasTexture;

out vec4 fragColor;

vec3 ACES(vec3 x){
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}

void main(){
    vec3 alb = (hasTexture != 0)
        ? pow(texture(tex, vUV).rgb, vec3(2.2))
        : gunColor;

    vec3  N   = normalize(vNorm);
    float d1  = max(dot(N, normalize(vec3(0.3, 0.8, 0.5))), 0.0);
    float d2  = max(dot(N, normalize(vec3(-0.5, 0.3, -0.8))), 0.0) * 0.25;
    float rim = pow(1.0 - max(dot(N, vec3(0,0,1)), 0.0), 3.0) * 0.15;
    vec3  H   = normalize(normalize(vec3(0.3, 0.8, 0.5)) + vec3(0,0,1));
    float sp  = pow(max(dot(H, N), 0.0), 64.0) * 0.4;

    vec3 lit = alb * (0.18 + d1*0.55 + d2 + rim)
             + vec3(sp * 0.6)
             + vec3(1.0, 0.85, 0.5) * flash * 0.3;

    fragColor = vec4(ACES(lit), 1.0);
}
)GLSL";

// ── Post-process ──────────────────────────────────────────────────
static const char* s_PostVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* s_PostFrag = R"GLSL(
#version 330 core
in vec2 vUV;

uniform sampler2D screenTex;
uniform vec2  resolution;
uniform float time;
uniform float hp01;

out vec4 fragColor;

void main(){
    vec2 px  = 1.0 / resolution;
    vec3 col = texture(screenTex, vUV).rgb;

    // Sharpening
    vec3 sh = col * 5.0
        - texture(screenTex, vUV + vec2(-1, 0)*px).rgb
        - texture(screenTex, vUV + vec2( 1, 0)*px).rgb
        - texture(screenTex, vUV + vec2( 0,-1)*px).rgb
        - texture(screenTex, vUV + vec2( 0, 1)*px).rgb;
    col = mix(col, sh, 0.18);

    // Contrast
    col = col*col*(3.0 - 2.0*col);

    // Cold shadows / warm highlights
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(col * vec3(0.78, 0.85, 1.0),
              col * vec3(1.00, 0.97, 0.90),
              clamp(lum * 2.0, 0.0, 1.0));

    // Slight desaturate
    float grey = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(grey), col, 0.85);

    // Gamma encode
    col = pow(max(col, vec3(0.0)), vec3(1.0/2.2));

    // Vignette
    float d = length(vUV - 0.5);
    col *= 1.0 - d*d*0.75;

    // Low HP damage pulse
    if(hp01 < 0.30){
        float p2    = sin(time * 2.5) * 0.5 + 0.5;
        float inten = (0.30 - hp01) / 0.30 * 0.50 * (0.5 + 0.5*p2);
        col = mix(col, vec3(col.r*0.5, 0.0, 0.0),
                  inten * smoothstep(0.20, 0.50, d));
    }

    // Film grain
    float noise = fract(sin(dot(vUV + fract(vec2(time)),
                    vec2(127.1, 311.7))) * 43758.5453);
    col += (noise - 0.5) * 0.012;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";

// ── Shadow map ────────────────────────────────────────────────────
static const char* s_ShadowVert = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aBlendIdx;
layout(location=4) in vec4 aBlendWt;

uniform mat4 lightMVP;
uniform int  skinned;
uniform mat4 bones[100];

void main(){
    vec4 pos = vec4(aPos, 1.0);
    if(skinned != 0){
        float ws = aBlendWt.x + aBlendWt.y + aBlendWt.z + aBlendWt.w;
        mat4  sk = mat4(0.0);
        if(ws < 0.001) sk = mat4(1.0);
        else for(int k=0; k<4; k++){
            int id = int(aBlendIdx[k]);
            if(id >= 0 && id < 100 && aBlendWt[k] > 0.0)
                sk += bones[id] * aBlendWt[k];
        }
        pos = sk * pos;
    }
    gl_Position = lightMVP * pos;
}
)GLSL";

static const char* s_ShadowFrag =
"#version 330 core\nvoid main(){}\n";

// ── Dot / Bullet-hole ─────────────────────────────────────────────
static const char* s_DotVert = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 mvp;
void main(){ gl_Position = mvp * vec4(aPos, 1.0); }
)GLSL";

static const char* s_DotFrag = R"GLSL(
#version 330 core
uniform vec4 color;
out vec4 fragColor;
void main(){ fragColor = color; }
)GLSL";

// ================================================================
//  HLSL ИСХОДНИКИ  (DX11 путь — D3DCompile в Renderer.h)
//  Та же логика что и GLSL выше.
// ================================================================

static const char* s_HlslWorld = R"HLSL(
cbuffer PerFrame:register(b0){
    matrix view, projection, lightSpaceMatrix;
    float3 lightDir; float _p0;
    float3 fogColor; float fogStart; float fogEnd;
    float3 camPos;   float _p1;
};
cbuffer PerObject:register(b1){
    matrix model, normalMatrix;
    float3 baseColor; int hasTexture;
};
Texture2D diffuse:register(t0);
Texture2D shadowMap:register(t1);
SamplerState            sLin:register(s0);
SamplerComparisonState  sCmp:register(s1);

struct V2P {
    float4 pos : SV_Position;
    float3 norm: NORMAL;
    float2 uv  : TEXCOORD0;
    float3 wp  : TEXCOORD1;
    float  fd  : TEXCOORD2;
    float4 lsp : TEXCOORD3;
};

V2P VSMain(float3 p:POSITION, float3 n:NORMAL, float2 uv:TEXCOORD0){
    V2P o;
    float4 wp = mul(model, float4(p,1));
    o.wp   = wp.xyz;
    o.norm = mul((float3x3)normalMatrix, n);
    o.uv   = uv;
    o.lsp  = mul(lightSpaceMatrix, wp);
    float4 vp = mul(view, wp);
    o.fd   = -vp.z;
    o.pos  = mul(projection, vp);
    return o;
}

float3 ACES(float3 x){ return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1); }

float4 PSMain(V2P i):SV_Target{
    float3 alb = hasTexture
        ? pow(diffuse.Sample(sLin,i.uv).rgb, 2.2)
        : baseColor;
    float3 N = normalize(i.norm);
    float3 L = normalize(-lightDir);
    float  NdL = max(dot(N,L), 0);

    float3 prj = i.lsp.xyz / i.lsp.w;
    prj = prj*0.5+0.5; prj.y = 1-prj.y;
    float shadow = 0;
    if(prj.z<=1 && all(prj.xy>0) && all(prj.xy<1)){
        float bias = max(.005*(1-NdL), .002);
        float2 ts; shadowMap.GetDimensions(ts.x,ts.y); ts = 1/ts;
        [unroll]for(int x=-1;x<=1;x++)
        [unroll]for(int y=-1;y<=1;y++)
            shadow += shadowMap.SampleCmpLevelZero(sCmp, prj.xy+float2(x,y)*ts, prj.z-bias);
        shadow = (9-shadow)/9*0.65;
    }

    float3 lit = alb*(
        float3(.25,.22,.20)
        + float3(1.05,.95,.80)*NdL*0.85*(1-shadow)
        + float3(.55,.70,.90)*max(dot(N,float3(0,1,0)),0)*0.25
        + float3(.40,.35,.28)*max(dot(N,float3(0,-1,0)),0)*0.12
    );
    lit += pow(max(dot(normalize(L+float3(0,0,1)),N),0),32)*0.15*(1-shadow);
    float grey = dot(lit, float3(.2126,.7152,.0722));
    lit = lerp(grey, lit, 1.2);
    lit = ACES(lit*0.8);
    float fogT = saturate((i.fd-fogStart)/(fogEnd-fogStart));
    fogT = fogT*fogT*fogT;
    lit = lerp(lit, fogColor, fogT);
    return float4(lit,1);
}
)HLSL";

static const char* s_HlslGun = R"HLSL(
cbuffer PF:register(b0){ matrix view, projection; };
cbuffer PO:register(b1){
    matrix model, normalMatrix;
    float3 gunColor; float flash;
    int hasTexture; int skinned; float2 _p;
};
cbuffer Bones:register(b2){ matrix bones[100]; };
Texture2D tex:register(t0); SamplerState sLin:register(s0);

struct VI { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0;
            float4 bi:BLENDINDICES; float4 bw:BLENDWEIGHT; };
struct V2P { float4 pos:SV_Position; float3 norm:NORMAL; float2 uv:TEXCOORD0; };

V2P VSMain(VI i){
    V2P o;
    float4 pos = float4(i.p,1); float3 nor = i.n;
    if(skinned){
        float4x4 skin = (float4x4)0;
        float ws = i.bw.x+i.bw.y+i.bw.z+i.bw.w;
        if(ws<.001) skin=(float4x4)1;
        else [unroll]for(int k=0;k<4;k++){
            int id=(int)i.bi[k];
            if(id>=0 && i.bw[k]>0) skin+=bones[id]*i.bw[k];
        }
        pos=mul(skin,pos); nor=mul((float3x3)skin,nor);
    }
    o.norm = mul((float3x3)normalMatrix, nor);
    o.uv   = i.uv;
    o.pos  = mul(projection, mul(view, mul(model, pos)));
    return o;
}

float3 ACES(float3 x){ return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1); }

float4 PSMain(V2P i):SV_Target{
    float3 alb = hasTexture ? pow(tex.Sample(sLin,i.uv).rgb,2.2) : gunColor;
    float3 N   = normalize(i.norm);
    float  d1  = max(dot(N, normalize(float3(.3,.8,.5))),  0);
    float  d2  = max(dot(N, normalize(float3(-.5,.3,-.8))),0)*0.25;
    float  rim = pow(1-max(dot(N,float3(0,0,1)),0),3)*0.15;
    float3 H   = normalize(normalize(float3(.3,.8,.5))+float3(0,0,1));
    float  sp  = pow(max(dot(H,N),0),64)*0.4;
    float3 lit = alb*(0.18+d1*0.55+d2+rim) + sp*0.6 + float3(1,.85,.5)*flash*0.3;
    return float4(ACES(lit),1);
}
)HLSL";

static const char* s_HlslPost = R"HLSL(
cbuffer CB:register(b0){ float2 resolution; float time; float hp01; };
Texture2D screen:register(t0); SamplerState sLin:register(s0);

struct V2P { float4 pos:SV_Position; float2 uv:TEXCOORD0; };

V2P VSMain(uint id:SV_VertexID){
    float2 p[6]={float2(-1,1),float2(1,1),float2(1,-1),
                 float2(-1,1),float2(1,-1),float2(-1,-1)};
    float2 u[6]={float2(0,0),float2(1,0),float2(1,1),
                 float2(0,0),float2(1,1),float2(0,1)};
    V2P o; o.pos=float4(p[id],0,1); o.uv=u[id]; return o;
}

float4 PSMain(V2P i):SV_Target{
    float2 uv=i.uv, px=1.0/resolution;
    float3 col=screen.Sample(sLin,uv).rgb;
    float3 sh=col*5
        -screen.Sample(sLin,uv+float2(-1,0)*px).rgb
        -screen.Sample(sLin,uv+float2( 1,0)*px).rgb
        -screen.Sample(sLin,uv+float2( 0, 1)*px).rgb
        -screen.Sample(sLin,uv+float2( 0,-1)*px).rgb;
    col=lerp(col,sh,0.18);
    col=col*col*(3.0-2.0*col);
    float lum=dot(col,float3(0.2126,0.7152,0.0722));
    col=lerp(col*float3(0.78,0.85,1.0),col*float3(1.0,0.97,0.90),saturate(lum*2.0));
    float grey=dot(col,float3(0.299,0.587,0.114));
    col=lerp((float3)grey,col,0.85);
    col=pow(max(col,0),1.0/2.2);
    float d=length(uv-0.5); col*=1.0-d*d*0.75;
    if(hp01<.30){
        float p2=sin(time*2.5)*.5+.5;
        float inten=(0.30-hp01)/.30*.50*(0.5+0.5*p2);
        col=lerp(col,float3(col.r*0.5,0,0),inten*smoothstep(.20,.50,d));
    }
    float noise=frac(sin(dot(uv+frac(time),float2(127.1,311.7)))*43758.5453);
    col+=(noise-.5)*.012;
    return float4(saturate(col),1);
}
)HLSL";

static const char* s_HlslShadow = R"HLSL(
cbuffer CB:register(b0){ matrix lightMVP; int skinned; float3 _p; };
cbuffer Bones:register(b1){ matrix bones[100]; };

struct VI { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0;
            float4 bi:BLENDINDICES; float4 bw:BLENDWEIGHT; };

float4 VSMain(VI i):SV_Position{
    float4 pos=float4(i.p,1);
    if(skinned){
        float4x4 skin=(float4x4)0;
        float ws=i.bw.x+i.bw.y+i.bw.z+i.bw.w;
        if(ws<.001) skin=(float4x4)1;
        else [unroll]for(int k=0;k<4;k++){
            int id=(int)i.bi[k];
            if(id>=0 && i.bw[k]>0) skin+=bones[id]*i.bw[k];
        }
        pos=mul(skin,pos);
    }
    return mul(lightMVP,pos);
}
float4 PSMain():SV_Target{ return float4(0,0,0,0); }
)HLSL";

static const char* s_HlslDot = R"HLSL(
cbuffer CB:register(b0){ matrix mvp; float4 color; };
float4 VSMain(float3 p:POSITION):SV_Position{ return mul(mvp,float4(p,1)); }
float4 PSMain():SV_Target{ return color; }
)HLSL";

// ================================================================
//  КОМПИЛЯТОР GL
// ================================================================
static GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        printf("[ST] Compile error:\n%s\n", log);
    }
    return sh;
}

static GLuint linkProgram(const char* vert, const char* frag) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        printf("[ST] Link error:\n%s\n", log);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (p) printf("[ST] Program %u OK\n", p);
    return p;
}

// ================================================================
//  РЕАЛИЗАЦИЯ ПУБЛИЧНОГО API
// ================================================================
namespace ShaderTranspiler {

    void init() {
        printf("[ST] Shader system ready. "
            "OpenGL=GLSL 330, DX11=HLSL, Vulkan=future SPIR-V.\n");
    }

    void shutdown() {
        printf("[ST] Shutdown.\n");
    }

    GLuint buildProgram(const char* hlsl) {
        // Определяем тип шейдера по уникальным строкам HLSL
        if (strstr(hlsl, "lightSpaceMatrix") && strstr(hlsl, "shadowMap"))
            return linkProgram(s_WorldVert, s_WorldFrag);
        if (strstr(hlsl, "lightMVP"))
            return linkProgram(s_ShadowVert, s_ShadowFrag);
        if (strstr(hlsl, "screenTex") || strstr(hlsl, "screen:register"))
            return linkProgram(s_PostVert, s_PostFrag);
        if (strstr(hlsl, "gunColor") || strstr(hlsl, "skinned"))
            return linkProgram(s_GunVert, s_GunFrag);
        // dot shader
        return linkProgram(s_DotVert, s_DotFrag);
    }

    const char* getWorldHLSL() { return s_HlslWorld; }
    const char* getGunHLSL() { return s_HlslGun; }
    const char* getPostHLSL() { return s_HlslPost; }
    const char* getShadowHLSL() { return s_HlslShadow; }
    const char* getDotHLSL() { return s_HlslDot; }

    std::vector<uint32_t> vsSpirv(const char*) { return {}; } // TODO: Vulkan
    std::vector<uint32_t> psSpirv(const char*) { return {}; }

    void clearCache() { /* нечего чистить — нет кэша на стороне */ }
    void reloadAll() { printf("[ST] Hot-reload not implemented yet.\n"); }

} // namespace ShaderTranspiler