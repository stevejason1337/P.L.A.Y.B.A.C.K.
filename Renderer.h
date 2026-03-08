#pragma once
// Renderer.h — единый рендерер с поддержкой OpenGL 3.3 и DirectX 11
// gRenderBackend задаётся в Settings.h до вызова renderer.init()

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <string>
#include "Settings.h"
#include "AnimatedModel.h"
#include "Player.h"
#include "WeaponManager.h"

// ════════════════════════════════════════════════════════════
//  GLSL ШЕЙДЕРЫ (OpenGL)
// ════════════════════════════════════════════════════════════
static const char* GLSL_WORLD_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNorm; layout(location=2) in vec2 aUV;
out vec3 vNorm,vWorldPos; out vec2 vUV; out float vFogDist; out vec4 vLSP;
uniform mat4 model,view,projection,lightSpaceMatrix; uniform mat3 normalMatrix;
void main(){
    vec4 wp=model*vec4(aPos,1); vWorldPos=wp.xyz; vNorm=normalMatrix*aNorm; vUV=aUV;
    vLSP=lightSpaceMatrix*wp; vec4 vp=view*wp; vFogDist=-vp.z; gl_Position=projection*vp;
})";
static const char* GLSL_WORLD_FRAG = R"(
#version 330 core
in vec3 vNorm,vWorldPos; in vec2 vUV; in float vFogDist; in vec4 vLSP;
out vec4 FragColor;
uniform vec3 lightDir,baseColor,fogColor,camPos; uniform float fogStart,fogEnd;
uniform bool hasTexture; uniform sampler2D tex,shadowMap;
vec3 ACES(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1);}
vec3 sat(vec3 c,float s){float l=dot(c,vec3(.2126,.7152,.0722));return mix(vec3(l),c,s);}
void main(){
    vec3 alb=hasTexture?pow(texture(tex,vUV).rgb,vec3(2.2)):baseColor;
    vec3 N=normalize(vNorm),L=normalize(-lightDir); float NdL=max(dot(N,L),0);
    vec3 proj=vLSP.xyz/vLSP.w; proj=proj*.5+.5;
    float shadow=0;
    if(proj.z<=1&&proj.x>0&&proj.x<1&&proj.y>0&&proj.y<1){
        float bias=max(.005*(1-NdL),.002); vec2 ts=1.0/vec2(textureSize(shadowMap,0));
        for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)shadow+=proj.z-bias>texture(shadowMap,proj.xy+vec2(x,y)*ts).r?1:0;
        shadow=shadow/9*.65;
    }
    vec3 lit=alb*(vec3(.25,.22,.20)+vec3(1.05,.95,.80)*NdL*.85*(1-shadow)
                 +vec3(.55,.70,.90)*max(dot(N,vec3(0,1,0)),0)*.25
                 +vec3(.40,.35,.28)*max(dot(N,vec3(0,-1,0)),0)*.12);
    lit+=pow(max(dot(normalize(L+vec3(0,0,1)),N),0),32)*.15*(1-shadow);
    lit=sat(lit,1.2); lit=ACES(lit*.8);
    float fogT=clamp((vFogDist-fogStart)/(fogEnd-fogStart),0,1); fogT=fogT*fogT*fogT;
    lit=mix(lit,fogColor,fogT); lit=pow(max(lit,vec3(0)),vec3(1.0/2.2));
    FragColor=vec4(lit,1);
})";
static const char* GLSL_GUN_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;  layout(location=3) in vec4 aBoneIDs; layout(location=4) in vec4 aW;
out vec3 vNorm; out vec2 vUV;
uniform mat4 model,view,projection; uniform mat3 normalMatrix;
uniform bool skinned; uniform mat4 bones[100];
void main(){
    vec4 pos=vec4(aPos,1); vec3 nor=aNorm;
    if(skinned){mat4 skin=mat4(0);for(int i=0;i<4;i++){int id=int(aBoneIDs[i]);if(id>=0&&aW[i]>0)skin+=bones[id]*aW[i];}float ws=aW.x+aW.y+aW.z+aW.w;if(ws<.001)skin=mat4(1);pos=skin*pos;nor=mat3(skin)*nor;}
    vNorm=normalMatrix*nor; vUV=aUV; gl_Position=projection*view*model*pos;
})";
static const char* GLSL_GUN_FRAG = R"(
#version 330 core
in vec3 vNorm; in vec2 vUV; out vec4 FragColor;
uniform bool hasTexture; uniform sampler2D tex; uniform vec3 gunColor; uniform float flash;
vec3 ACES(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1);}
void main(){
    vec3 alb=hasTexture?pow(texture(tex,vUV).rgb,vec3(2.2)):gunColor;
    vec3 N=normalize(vNorm);
    float d1=max(dot(N,normalize(vec3(.3,.8,.5))),0); float d2=max(dot(N,normalize(vec3(-.5,.3,-.8))),0)*.25;
    float rim=pow(1-max(dot(N,vec3(0,0,1)),0),3)*.15;
    float sp=pow(max(dot(normalize(normalize(vec3(.3,.8,.5))+vec3(0,0,1)),N),0),64)*.4;
    vec3 lit=alb*(.18+d1*.55+d2+rim)+sp*.6+vec3(1,.85,.5)*flash*.3;
    lit=ACES(lit); lit=pow(max(lit,vec3(0)),vec3(1.0/2.2)); FragColor=vec4(lit,1);
})";
static const char* GLSL_POST_VERT = R"(
#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV;
out vec2 vUV; void main(){vUV=aUV;gl_Position=vec4(aPos,0,1);})";
static const char* GLSL_POST_FRAG = R"(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D screenTex; uniform vec2 resolution; uniform float time,hp01;
void main(){
    vec2 uv=vUV,px=1.0/resolution;
    vec3 col=texture(screenTex,uv).rgb;
    vec3 sharp=col*5-texture(screenTex,uv+vec2(-1,0)*px).rgb-texture(screenTex,uv+vec2(1,0)*px).rgb
              -texture(screenTex,uv+vec2(0,1)*px).rgb-texture(screenTex,uv+vec2(0,-1)*px).rgb;
    col=mix(col,sharp,.18); float lum=dot(col,vec3(.2126,.7152,.0722)); col=mix(vec3(lum),col,1.15);
    col=(col-.5)*1.08+.5; float dist=length(uv-.5); col*=1-dist*dist*.40;
    if(hp01<.30){float p=sin(time*2.5)*.5+.5;float inten=(0.30-hp01)/.30*.45*(.5+.5*p);col=mix(col,vec3(col.r,col.g*.2,col.b*.2),inten*smoothstep(.25,.5,dist));}
    col+=(fract(sin(dot(uv+fract(time),vec2(127.1,311.7)))*43758.5453)-.5)*.012;
    FragColor=vec4(clamp(col,0,1),1);
})";
static const char* GLSL_SHADOW_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=3) in vec4 aBI; layout(location=4) in vec4 aW;
uniform mat4 lightMVP; uniform bool skinned; uniform mat4 bones[100];
void main(){
    vec4 pos=vec4(aPos,1);
    if(skinned){mat4 skin=mat4(0);for(int i=0;i<4;i++){int id=int(aBI[i]);if(id>=0&&aW[i]>0)skin+=bones[id]*aW[i];}float ws=aW.x+aW.y+aW.z+aW.w;if(ws<.001)skin=mat4(1);pos=skin*pos;}
    gl_Position=lightMVP*pos;
})";
static const char* GLSL_SHADOW_FRAG = "#version 330 core\nvoid main(){}";
static const char* GLSL_DOT_VERT = "#version 330 core\nlayout(location=0)in vec3 aPos;uniform mat4 mvp;void main(){gl_Position=mvp*vec4(aPos,1);}";
static const char* GLSL_DOT_FRAG = "#version 330 core\nout vec4 FragColor;uniform vec4 color;void main(){FragColor=color;}";

static GLuint _glBuild(const char* v, const char* f) {
    auto cc = [](GLenum t, const char* s)->GLuint {GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh); int ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, NULL, l); printf("[GL]%s\n", l); }return sh; };
    GLuint vs = cc(GL_VERTEX_SHADER, v), fs = cc(GL_FRAGMENT_SHADER, f);
    GLuint p = glCreateProgram(); glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs); return p;
}

// ════════════════════════════════════════════════════════════
//  DIRECTX 11 BACKEND (только Windows)
// ════════════════════════════════════════════════════════════
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxgi.lib")
using Microsoft::WRL::ComPtr;

// ── HLSL шейдеры ─────────────────────────────────────────────
static const char* HLSL_WORLD = R"(
cbuffer PerFrame:register(b0){matrix view,projection,lightSpaceMatrix;float3 lightDir;float _p0;float3 fogColor;float fogStart;float fogEnd;float3 camPos;float _p1;};
cbuffer PerObject:register(b1){matrix model,normalMatrix;float3 baseColor;int hasTexture;};
Texture2D diffuse:register(t0);SamplerState sLin:register(s0);
struct V2P{float4 pos:SV_Position;float3 norm:NORMAL;float2 uv:TEXCOORD0;float fd:TEXCOORD1;};
V2P VSMain(float3 p:POSITION,float3 n:NORMAL,float2 uv:TEXCOORD0){
    V2P o;float4 wp=mul(model,float4(p,1));o.norm=mul((float3x3)normalMatrix,n);
    o.uv=uv;float4 vp=mul(view,wp);o.fd=-vp.z;o.pos=mul(projection,vp);return o;
}
float3 ACES(float3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1);}
float3 sat3(float3 c,float s){float l=dot(c,float3(.2126,.7152,.0722));return lerp(l,c,s);}
float4 PSMain(V2P i):SV_Target{
    float3 alb=hasTexture?pow(diffuse.Sample(sLin,i.uv).rgb,2.2):baseColor;
    float3 N=normalize(i.norm),L=normalize(-lightDir);float NdL=max(dot(N,L),0);
    float3 lit=alb*(float3(.30,.28,.25)+float3(1.05,.95,.80)*NdL*.85+float3(.55,.70,.90)*max(dot(N,float3(0,1,0)),0)*.25+float3(.40,.35,.28)*max(dot(N,float3(0,-1,0)),0)*.12);
    lit+=pow(max(dot(normalize(L+float3(0,0,1)),N),0),32)*.15;
    lit=sat3(lit,1.2);lit=ACES(lit*.8);
    float fogT=saturate((i.fd-fogStart)/(fogEnd-fogStart));fogT=fogT*fogT*fogT;
    lit=lerp(lit,fogColor,fogT);lit=pow(max(lit,0),1.0/2.2);return float4(lit,1);
})";
static const char* HLSL_GUN = R"(
cbuffer PF:register(b0){matrix view,projection;};
cbuffer PO:register(b1){matrix model,normalMatrix;float3 gunColor;float flash;int hasTexture;int skinned;float2 _p;};
cbuffer Bones:register(b2){matrix bones[100];};
Texture2D tex:register(t0);SamplerState sLin:register(s0);
struct VI{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 bi:BLENDINDICES;float4 bw:BLENDWEIGHT;};
struct V2P{float4 pos:SV_Position;float3 norm:NORMAL;float2 uv:TEXCOORD0;};
V2P VSMain(VI i){
    V2P o;float4 pos=float4(i.p,1);float3 nor=i.n;
    if(skinned){float4x4 skin=(float4x4)0;float ws=i.bw.x+i.bw.y+i.bw.z+i.bw.w;
        if(ws<.001)skin=(float4x4)1;
        else{[unroll]for(int k=0;k<4;k++){int id=(int)i.bi[k];if(id>=0&&i.bw[k]>0)skin+=bones[id]*i.bw[k];}}
        pos=mul(skin,pos);nor=mul((float3x3)skin,nor);}
    o.norm=mul((float3x3)normalMatrix,nor);o.uv=i.uv;o.pos=mul(projection,mul(view,mul(model,pos)));return o;
}
float3 ACES(float3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1);}
float4 PSMain(V2P i):SV_Target{
    float3 alb=hasTexture?pow(tex.Sample(sLin,i.uv).rgb,2.2):gunColor;
    float3 N=normalize(i.norm);
    float d1=max(dot(N,normalize(float3(.3,.8,.5))),0);float d2=max(dot(N,normalize(float3(-.5,.3,-.8))),0)*.25;
    float rim=pow(1-max(dot(N,float3(0,0,1)),0),3)*.15;
    float sp=pow(max(dot(normalize(normalize(float3(.3,.8,.5))+float3(0,0,1)),N),0),64)*.4;
    float3 lit=alb*(.18+d1*.55+d2+rim)+sp*.6+float3(1,.85,.5)*flash*.3;
    lit=ACES(lit);lit=pow(max(lit,0),1.0/2.2);return float4(lit,1);
})";
static const char* HLSL_POST = R"(
cbuffer CB:register(b0){float2 resolution;float time;float hp01;};
Texture2D screen:register(t0);SamplerState sLin:register(s0);
struct V2P{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V2P VSMain(uint id:SV_VertexID){
    float2 p[6]={float2(-1,1),float2(1,1),float2(1,-1),float2(-1,1),float2(1,-1),float2(-1,-1)};
    float2 u[6]={float2(0,0),float2(1,0),float2(1,1),float2(0,0),float2(1,1),float2(0,1)};
    V2P o;o.pos=float4(p[id],0,1);o.uv=u[id];return o;
}
float4 PSMain(V2P i):SV_Target{
    float2 uv=i.uv,px=1.0/resolution;
    float3 col=screen.Sample(sLin,uv).rgb;
    float3 sharp=col*5-screen.Sample(sLin,uv+float2(-1,0)*px).rgb-screen.Sample(sLin,uv+float2(1,0)*px).rgb
                     -screen.Sample(sLin,uv+float2(0,1)*px).rgb-screen.Sample(sLin,uv+float2(0,-1)*px).rgb;
    col=lerp(col,sharp,.18);float lum=dot(col,float3(.2126,.7152,.0722));col=lerp(lum,col,1.15);
    col=(col-.5)*1.08+.5;float dist=length(uv-.5);col*=1-dist*dist*.40;
    if(hp01<.30){float p2=sin(time*2.5)*.5+.5;float inten=(0.30-hp01)/.30*.45*(.5+.5*p2);col=lerp(col,float3(col.r,col.g*.2,col.b*.2),inten*smoothstep(.25,.5,dist));}
    float noise=frac(sin(dot(uv+frac(time),float2(127.1,311.7)))*43758.5453);
    col+=(noise-.5)*.012;return float4(saturate(col),1);
})";
static const char* HLSL_SHADOW = R"(
cbuffer CB:register(b0){matrix lightMVP;int skinned;float3 _p;};
cbuffer Bones:register(b1){matrix bones[100];};
struct VI{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 bi:BLENDINDICES;float4 bw:BLENDWEIGHT;};
float4 VSMain(VI i):SV_Position{
    float4 pos=float4(i.p,1);
    if(skinned){float4x4 skin=(float4x4)0;float ws=i.bw.x+i.bw.y+i.bw.z+i.bw.w;
        if(ws<.001)skin=(float4x4)1;
        else{[unroll]for(int k=0;k<4;k++){int id=(int)i.bi[k];if(id>=0&&i.bw[k]>0)skin+=bones[id]*i.bw[k];}}
        pos=mul(skin,pos);}
    return mul(lightMVP,pos);
}
float4 PSMain():SV_Target{return float4(0,0,0,0);}
)";
static const char* HLSL_DOT = R"(
cbuffer CB:register(b0){matrix mvp;float4 color;};
float4 VSMain(float3 p:POSITION):SV_Position{return mul(mvp,float4(p,1));}
float4 PSMain():SV_Target{return color;}
)";

// ── DX11 State ────────────────────────────────────────────────
struct DX11State {
    ComPtr<ID3D11Device>           dev;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<IDXGISwapChain>         sc;
    ComPtr<ID3D11RenderTargetView> bbRTV;
    // Post-process offscreen
    ComPtr<ID3D11Texture2D>        postTex, postDepth;
    ComPtr<ID3D11RenderTargetView> postRTV;
    ComPtr<ID3D11ShaderResourceView> postSRV;
    ComPtr<ID3D11DepthStencilView> postDSV;
    // Shadow map
    ComPtr<ID3D11Texture2D>        shadowTex;
    ComPtr<ID3D11DepthStencilView> shadowDSV;
    ComPtr<ID3D11ShaderResourceView> shadowSRV;
    // States
    ComPtr<ID3D11RasterizerState>  rsNorm, rsNoCull, rsShadow, rsWireframe;
    ComPtr<ID3D11DepthStencilState>dssOn, dssOff;
    ComPtr<ID3D11BlendState>       bsOpaque, bsAlpha;
    ComPtr<ID3D11SamplerState>     sampLin, sampCmp;
    // Constant buffers
    ComPtr<ID3D11Buffer>           cbWF, cbWO, cbGF, cbGO, cbBones, cbPost, cbShadow, cbDot;
    struct Sh { ComPtr<ID3D11VertexShader>vs; ComPtr<ID3D11PixelShader>ps; ComPtr<ID3D11InputLayout>il; };
    Sh world, gun, shadow, dot, post;
    bool ready = false;
    bool wireframe = false; // DX11 wireframe флаг
} dx11;

// ── CB structs (16-byte aligned) ─────────────────────────────
struct alignas(16) DX_WF { glm::mat4 view, proj, ls; glm::vec3 ld; float _0; glm::vec3 fc; float fs; float fe; glm::vec3 cp; float _1; };
struct alignas(16) DX_WO { glm::mat4 model, nm; glm::vec3 bc; int ht; };
struct alignas(16) DX_GF { glm::mat4 view, proj; };
struct alignas(16) DX_GO { glm::mat4 model, nm; glm::vec3 gc; float flash; int ht; int sk; glm::vec2 _p; };
struct alignas(16) DX_Post { glm::vec2 res; float time; float hp; };
struct alignas(16) DX_Shadow { glm::mat4 lmvp; int sk; glm::vec3 _p; };
struct alignas(16) DX_Dot { glm::mat4 mvp; glm::vec4 color; };

static void _dxCB(ComPtr<ID3D11Buffer>& b, UINT sz) {
    D3D11_BUFFER_DESC d = {}; d.ByteWidth = ((sz + 15) / 16) * 16; d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dx11.dev->CreateBuffer(&d, nullptr, &b);
}
static void _dxUp(ComPtr<ID3D11Buffer>& b, const void* data, size_t sz) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(dx11.ctx->Map(b.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, data, sz); dx11.ctx->Unmap(b.Get(), 0); }
}
static ComPtr<ID3DBlob> _dxC(const char* src, const char* e, const char* t) {
    ComPtr<ID3DBlob>b, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, e, t, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &err);
    if (FAILED(hr) && err)printf("[DX11] Shader error (%s):\n%s\n", e, (char*)err->GetBufferPointer());
    return b;
}
static void _dxSh(DX11State::Sh& sh, const char* src, bool il = true, const char* vs = "VSMain", const char* ps = "PSMain") {
    static D3D11_INPUT_ELEMENT_DESC lay[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    auto vb = _dxC(src, vs, "vs_5_0"); auto pb = _dxC(src, ps, "ps_5_0");
    if (!vb || !pb)return;
    dx11.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &sh.vs);
    dx11.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &sh.ps);
    if (il)dx11.dev->CreateInputLayout(lay, 5, vb->GetBufferPointer(), vb->GetBufferSize(), &sh.il);
}
static void _dxMesh(const GPUMesh& m) {
    if (!m.dxVB || !m.dxIB)return;
    UINT st = m.dxStride, off = 0;
    ID3D11Buffer* vb = static_cast<ID3D11Buffer*>(m.dxVB);
    ID3D11Buffer* ib = static_cast<ID3D11Buffer*>(m.dxIB);
    dx11.ctx->IASetVertexBuffers(0, 1, &vb, &st, &off);
    dx11.ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
    dx11.ctx->DrawIndexed(m.indexCount, 0, 0);
}

static bool _dxInit(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {}; scd.BufferCount = 2; scd.BufferDesc.Width = SCR_WIDTH; scd.BufferDesc.Height = SCR_HEIGHT;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; scd.BufferDesc.RefreshRate = { 60,1 };
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1; scd.Windowed = TRUE; scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &fl, 1, D3D11_SDK_VERSION, &scd, &dx11.sc, &dx11.dev, nullptr, &dx11.ctx);
    if (FAILED(hr)) { printf("[DX11] Device create FAILED 0x%08X\n", hr); return false; }

    { ComPtr<ID3D11Texture2D>bb; dx11.sc->GetBuffer(0, IID_PPV_ARGS(&bb)); dx11.dev->CreateRenderTargetView(bb.Get(), nullptr, &dx11.bbRTV); }

    auto mkTex2D = [&](ComPtr<ID3D11Texture2D>& tex, int w, int h, DXGI_FORMAT fmt, UINT bind) {
        D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = fmt; d.SampleDesc.Count = 1; d.BindFlags = bind;
        dx11.dev->CreateTexture2D(&d, nullptr, &tex);
        };
    mkTex2D(dx11.postTex, SCR_WIDTH, SCR_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    dx11.dev->CreateRenderTargetView(dx11.postTex.Get(), nullptr, &dx11.postRTV);
    dx11.dev->CreateShaderResourceView(dx11.postTex.Get(), nullptr, &dx11.postSRV);
    mkTex2D(dx11.postDepth, SCR_WIDTH, SCR_HEIGHT, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL);
    dx11.dev->CreateDepthStencilView(dx11.postDepth.Get(), nullptr, &dx11.postDSV);
    mkTex2D(dx11.shadowTex, 2048, 2048, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
    { D3D11_DEPTH_STENCIL_VIEW_DESC d = {}; d.Format = DXGI_FORMAT_D32_FLOAT; d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; dx11.dev->CreateDepthStencilView(dx11.shadowTex.Get(), &d, &dx11.shadowDSV); }
    { D3D11_SHADER_RESOURCE_VIEW_DESC d = {}; d.Format = DXGI_FORMAT_R32_FLOAT; d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels = 1; dx11.dev->CreateShaderResourceView(dx11.shadowTex.Get(), &d, &dx11.shadowSRV); }

    // Rasterizer states
    {
        D3D11_RASTERIZER_DESC d = {}; d.FillMode = D3D11_FILL_SOLID; d.CullMode = D3D11_CULL_NONE; d.FrontCounterClockwise = TRUE; d.DepthClipEnable = TRUE;
        dx11.dev->CreateRasterizerState(&d, &dx11.rsNorm);
        d.CullMode = D3D11_CULL_NONE; dx11.dev->CreateRasterizerState(&d, &dx11.rsNoCull);
        d.CullMode = D3D11_CULL_BACK; d.DepthBias = 1000; d.SlopeScaledDepthBias = 2.f; dx11.dev->CreateRasterizerState(&d, &dx11.rsShadow);
        // Wireframe
        d.FillMode = D3D11_FILL_WIREFRAME; d.CullMode = D3D11_CULL_NONE; d.DepthBias = 0; d.SlopeScaledDepthBias = 0;
        dx11.dev->CreateRasterizerState(&d, &dx11.rsWireframe);
    }
    {
        D3D11_DEPTH_STENCIL_DESC d = {}; d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D11_COMPARISON_LESS;
        dx11.dev->CreateDepthStencilState(&d, &dx11.dssOn); d.DepthEnable = FALSE; dx11.dev->CreateDepthStencilState(&d, &dx11.dssOff);
    }
    {
        D3D11_BLEND_DESC d = {}; d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dx11.dev->CreateBlendState(&d, &dx11.bsOpaque);
        d.RenderTarget[0].BlendEnable = TRUE; d.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; d.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        d.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; dx11.dev->CreateBlendState(&d, &dx11.bsAlpha);
    }
    {
        D3D11_SAMPLER_DESC d = {}; d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; d.MaxLOD = D3D11_FLOAT32_MAX; d.ComparisonFunc = D3D11_COMPARISON_ALWAYS; dx11.dev->CreateSamplerState(&d, &dx11.sampLin);
        d.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; d.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL; dx11.dev->CreateSamplerState(&d, &dx11.sampCmp);
    }

    _dxCB(dx11.cbWF, sizeof(DX_WF)); _dxCB(dx11.cbWO, sizeof(DX_WO));
    _dxCB(dx11.cbGF, sizeof(DX_GF)); _dxCB(dx11.cbGO, sizeof(DX_GO));
    _dxCB(dx11.cbBones, 6400); _dxCB(dx11.cbPost, sizeof(DX_Post));
    _dxCB(dx11.cbShadow, sizeof(DX_Shadow)); _dxCB(dx11.cbDot, sizeof(DX_Dot));

    printf("[DX11] Compiling shaders...\n");
    _dxSh(dx11.world, HLSL_WORLD); _dxSh(dx11.gun, HLSL_GUN);
    _dxSh(dx11.shadow, HLSL_SHADOW); _dxSh(dx11.dot, HLSL_DOT);
    {
        auto vb = _dxC(HLSL_POST, "VSMain", "vs_5_0"); auto pb = _dxC(HLSL_POST, "PSMain", "ps_5_0");
        if (vb && pb) { dx11.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &dx11.post.vs); dx11.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &dx11.post.ps); }
    }

    dx11.ready = true;
    printf("[DX11] Init OK\n");
    return true;
}
#endif // _WIN32

// ════════════════════════════════════════════════════════════
//  RENDERER
// ════════════════════════════════════════════════════════════
extern bool isADS;
inline constexpr float ADS_SPEED = 8.f;

struct Renderer
{
    // OpenGL IDs
    GLuint worldShader = 0, gunShader = 0, dotShader = 0, postShader = 0, shadowShader = 0;
    GLuint fbo = 0, fboTex = 0, fboRBO = 0, quadVAO = 0, quadVBO = 0;
    GLuint dotVAO = 0, floorVAO = 0, floorVBO = 0, shadowFBO = 0, shadowTex = 0;
    static constexpr int SHADOW_W = 2048, SHADOW_H = 2048;
    struct WL { int model, view, proj, nm, ld, bc, ht, tex, fc, fs, fe, cp; }wl{};
    struct GL2 { int model, view, proj, nm, sk, bones, ht, tex, gc, flash; }gl{};
    struct DL { int mvp, color; }dl{};

    glm::mat4 lightSpaceMatrix{ 1.f };
    glm::vec3 sunDir = glm::normalize(glm::vec3(0.4f, -1.f, 0.3f));
    float postTime = 0.f, postHp01 = 1.f;
    int   lastFireCounter = 0;
    bool  reloadStarted = false;
    float reloadTimer = 0.f, reloadDuration = 2.5f;

    // ── Wireframe — одинаково работает на обоих API ──────────
    // Вызывай когда wireframe флаг изменился
    void setWireframe(bool on)
    {
#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11 && dx11.ready) {
            dx11.wireframe = on;
            return;
        }
#endif
        glPolygonMode(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
    }

    // ── Проверка состояния DX11 ──────────────────────────────
    bool isDX11Ready() const {
#ifdef _WIN32
        return dx11.ready;
#else
        return false;
#endif
    }

    // ── Получить DX11 Device / Context (для upload и ImGui) ──
    void* getDX11Device() const {
#ifdef _WIN32
        return dx11.ready ? (void*)dx11.dev.Get() : nullptr;
#else
        return nullptr;
#endif
    }
    void* getDX11Context() const {
#ifdef _WIN32
        return dx11.ready ? (void*)dx11.ctx.Get() : nullptr;
#else
        return nullptr;
#endif
    }

    const char* backendName() const {
#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11 && dx11.ready) return "DirectX 11";
#endif
        return "OpenGL";
    }

    // ── init ─────────────────────────────────────────────────
    void init(void* windowHandle = nullptr)
    {
#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11) {
            if (_dxInit((HWND)windowHandle)) { printf("[Renderer] Backend: DirectX 11\n"); return; }
            printf("[Renderer] DX11 failed, falling back to OpenGL\n");
            gRenderBackend = RenderBackend::OpenGL;
            saveEngineConfig(); // сохраняем фоллбэк
        }
#endif
        _initGL();
        printf("[Renderer] Backend: OpenGL\n");
    }

    // ── Shadow pass ──────────────────────────────────────────
    void renderShadowPass(const std::vector<GPUMesh>& mm, const glm::vec3& cam)
    {
        glm::mat4 lv = glm::lookAt(cam - sunDir * 40.f, cam, glm::vec3(0, 1, 0));
        lightSpaceMatrix = glm::ortho(-30.f, 30.f, -30.f, 30.f, 1.f, 120.f) * lv;
#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11 && dx11.ready) {
            D3D11_VIEWPORT vp = { 0,0,2048,2048,0,1 }; dx11.ctx->RSSetViewports(1, &vp);
            dx11.ctx->OMSetRenderTargets(0, nullptr, dx11.shadowDSV.Get());
            dx11.ctx->ClearDepthStencilView(dx11.shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            dx11.ctx->VSSetShader(dx11.shadow.vs.Get(), nullptr, 0); dx11.ctx->PSSetShader(dx11.shadow.ps.Get(), nullptr, 0);
            dx11.ctx->IASetInputLayout(dx11.shadow.il.Get()); dx11.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            dx11.ctx->RSSetState(dx11.rsShadow.Get());
            DX_Shadow cb; cb.lmvp = lightSpaceMatrix; cb.sk = 0; _dxUp(dx11.cbShadow, &cb, sizeof(cb));
            dx11.ctx->VSSetConstantBuffers(0, 1, dx11.cbShadow.GetAddressOf());
            for (auto& m : mm)_dxMesh(m);
            D3D11_VIEWPORT vp2 = { 0,0,(float)SCR_WIDTH,(float)SCR_HEIGHT,0,1 }; dx11.ctx->RSSetViewports(1, &vp2);
            return;
        }
#endif
        glViewport(0, 0, SHADOW_W, SHADOW_H); glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO); glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(shadowShader); glUniformMatrix4fv(glGetUniformLocation(shadowShader, "lightMVP"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        glUniform1i(glGetUniformLocation(shadowShader, "skinned"), 0);
        for (auto& m : mm) { glBindVertexArray(m.VAO); glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0); }
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    }

    // ── Begin frame ──────────────────────────────────────────
    void beginFrame()
    {
#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11 && dx11.ready) {
            float c[4] = { 0.68f,0.65f,0.60f,1.f };
            dx11.ctx->OMSetRenderTargets(1, dx11.postRTV.GetAddressOf(), dx11.postDSV.Get());
            dx11.ctx->ClearRenderTargetView(dx11.postRTV.Get(), c);
            dx11.ctx->ClearDepthStencilView(dx11.postDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            D3D11_VIEWPORT vp = { 0,0,(float)SCR_WIDTH,(float)SCR_HEIGHT,0,1 }; dx11.ctx->RSSetViewports(1, &vp);
            dx11.ctx->OMSetDepthStencilState(dx11.dssOn.Get(), 0); return;
        }
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }

    // ── Draw scene ───────────────────────────────────────────
    void drawScene(const std::vector<GPUMesh>& mm, AnimatedModel& gm,
        glm::mat4 mapT, const glm::vec3& cam, const glm::vec3& cf, const glm::vec3& cu)
    {
        float adsTarget = isADS ? 1.f : 0.f;
        gun.adsProgress += (adsTarget - gun.adsProgress) * ADS_SPEED * (1.f / 60.f);
        gun.adsProgress = glm::clamp(gun.adsProgress, 0.f, 1.f);
        glm::mat4 view = glm::lookAt(cam, cam + cf, cu);
        glm::mat4 proj = glm::perspective(glm::radians(glm::mix(FOV, FOV * 0.6f, gun.adsProgress)), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.15f, 2000.f);
        glm::mat4 projG = glm::perspective(glm::radians(GUN_FOV), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.01f, 100.f);

        glm::vec3 right = glm::normalize(glm::cross(cf, cu)), up2 = glm::normalize(glm::cross(right, cf));
        bool mov = glm::length(glm::vec2(player.vel.x, player.vel.z)) > 0.5f;
        float bobX = mov ? sinf(gun.bobTimer) * .003f : 0.f, bobY = mov ? cosf(gun.bobTimer * 2.f) * .002f : 0.f;
        if (mov && player.onGround)gun.bobTimer += 0.016f * 6.f;
        const WeaponDef& def = weaponManager.activeDef(); float adsP = gun.adsProgress;
        glm::vec3 gPos = cam + right * (glm::mix(GUN_OFFSET_RIGHT + def.posRight, 0.f, adsP) + bobX * (1 - adsP))
            + up2 * (GUN_OFFSET_UP + def.posUp + bobY * (1 - adsP) + gun.recoilOffset) + cf * (GUN_OFFSET_FWD + def.posFwd);
        glm::mat4 gMat(1.f); gMat[0] = glm::vec4(right, 0.f); gMat[1] = glm::vec4(up2, 0.f); gMat[2] = glm::vec4(-cf, 0.f); gMat[3] = glm::vec4(gPos, 1.f);
        gMat = glm::rotate(gMat, glm::radians(def.rotY), glm::vec3(0, 1, 0));
        gMat = glm::rotate(gMat, glm::radians(def.rotX), glm::vec3(1, 0, 0));
        gMat = glm::scale(gMat, glm::vec3(def.scale));

#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11 && dx11.ready) { _dxScene(mm, gm, mapT, cam, view, proj, projG, gMat); return; }
#endif
        _glScene(mm, gm, mapT, cam, view, proj, projG, gMat);
    }

    // ── End frame ────────────────────────────────────────────
    void endFrame(float dt = 0.016f)
    {
        postTime += dt;
#ifdef _WIN32
        if (gRenderBackend == RenderBackend::DX11 && dx11.ready) {
            dx11.ctx->OMSetRenderTargets(1, dx11.bbRTV.GetAddressOf(), nullptr);
            float c[4] = { 0,0,0,1 }; dx11.ctx->ClearRenderTargetView(dx11.bbRTV.Get(), c);
            dx11.ctx->OMSetDepthStencilState(dx11.dssOff.Get(), 0);
            dx11.ctx->VSSetShader(dx11.post.vs.Get(), nullptr, 0); dx11.ctx->PSSetShader(dx11.post.ps.Get(), nullptr, 0);
            dx11.ctx->IASetInputLayout(nullptr); dx11.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            dx11.ctx->RSSetState(dx11.rsNoCull.Get());
            DX_Post cbp; cbp.res = { SCR_WIDTH,SCR_HEIGHT }; cbp.time = postTime; cbp.hp = postHp01;
            _dxUp(dx11.cbPost, &cbp, sizeof(cbp)); dx11.ctx->PSSetConstantBuffers(0, 1, dx11.cbPost.GetAddressOf());
            dx11.ctx->PSSetShaderResources(0, 1, dx11.postSRV.GetAddressOf()); dx11.ctx->PSSetSamplers(0, 1, dx11.sampLin.GetAddressOf());
            dx11.ctx->Draw(6, 0);
            ID3D11ShaderResourceView* n = nullptr; dx11.ctx->PSSetShaderResources(0, 1, &n);
            dx11.sc->Present(1, 0); return;
        }
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDisable(GL_DEPTH_TEST); glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(postShader); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fboTex);
        glUniform1i(glGetUniformLocation(postShader, "screenTex"), 0);
        glUniform2f(glGetUniformLocation(postShader, "resolution"), (float)SCR_WIDTH, (float)SCR_HEIGHT);
        glUniform1f(glGetUniformLocation(postShader, "time"), postTime); glUniform1f(glGetUniformLocation(postShader, "hp01"), postHp01);
        glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLES, 0, 6); glEnable(GL_DEPTH_TEST);
    }

    // ── Weapon anim (одинаково для обоих API) ────────────────
    void updateGunAnim(AnimatedModel& gm, float dt)
    {
        const WeaponDef& def = weaponManager.activeDef();
        if (fireAnimCounter != lastFireCounter) { lastFireCounter = fireAnimCounter; int v = fireAnimCounter % 3; const std::string& fa = (v == 0) ? def.animFire : (v == 1) ? def.animFire001 : def.animFire002; if (!fa.empty() && gm.hasAnim(fa))gm.playOnce(fa, def.animIdle); }
        if (gun.reloading) {
            if (!reloadStarted) { reloadTimer = 0.f; const std::string& ra = gun.reloadFull ? def.animReloadFull : def.animReloadEasy; if (!ra.empty() && gm.hasAnim(ra))gm.playOnce(ra, def.animIdle); reloadStarted = true; }
            reloadTimer += dt; if (gm.isDone() || reloadTimer >= reloadDuration)_finishReload(gm, def.animIdle);
        }
        if (!gun.reloading) {
            bool mov = glm::length(glm::vec2(player.vel.x, player.vel.z)) > 0.5f && player.onGround;
            if (gm.curAnim == def.animIdle && mov && !def.animWalk.empty() && gm.hasAnim(def.animWalk))gm.play(def.animWalk, true);
            if (!def.animWalk.empty() && gm.curAnim == def.animWalk && !mov && gm.hasAnim(def.animIdle))gm.play(def.animIdle, true);
        }
        gm.update(dt);
    }
    void onWeaponSwitch() { reloadStarted = false; reloadTimer = 0.f; lastFireCounter = fireAnimCounter; }

private:
    void _finishReload(AnimatedModel& gm, const std::string& ia) { gun.ammo = weaponManager.activeDef().maxAmmo; gun.reloading = false; gun.shootCooldown = 0.f; reloadStarted = false; reloadTimer = 0.f; gm.animDone = false; gm.looping = true; gm.curAnim = ""; if (!ia.empty() && gm.hasAnim(ia))gm.play(ia, true); }

    // ════════════════════════════════════════════════════
    //  GL PRIVATE
    // ════════════════════════════════════════════════════
    void _initGL()
    {
        worldShader = _glBuild(GLSL_WORLD_VERT, GLSL_WORLD_FRAG);
        gunShader = _glBuild(GLSL_GUN_VERT, GLSL_GUN_FRAG);
        dotShader = _glBuild(GLSL_DOT_VERT, GLSL_DOT_FRAG);
        postShader = _glBuild(GLSL_POST_VERT, GLSL_POST_FRAG);
        shadowShader = _glBuild(GLSL_SHADOW_VERT, GLSL_SHADOW_FRAG);

        glGenFramebuffers(1, &shadowFBO); glGenTextures(1, &shadowTex);
        glBindTexture(GL_TEXTURE_2D, shadowTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float bc[] = { 1,1,1,1 }; glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bc);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE); glBindFramebuffer(GL_FRAMEBUFFER, 0);

        float qv[] = { -1,-1,0,0,1,-1,1,0,1,1,1,1,-1,-1,0,0,1,1,1,1,-1,1,0,1 };
        glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO); glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(qv), qv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float))); glEnableVertexAttribArray(1);
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &fboTex); glBindTexture(GL_TEXTURE_2D, fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
        glGenRenderbuffers(1, &fboRBO); glBindRenderbuffer(GL_RENDERBUFFER, fboRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, SCR_WIDTH, SCR_HEIGHT);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fboRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        float fv[] = { -50,0,-50,0,1,0,0,0,50,0,-50,0,1,0,1,0,50,0,50,0,1,0,1,1,-50,0,-50,0,1,0,0,0,50,0,50,0,1,0,1,1,-50,0,50,0,1,0,0,1 };
        glGenVertexArrays(1, &floorVAO); glGenBuffers(1, &floorVBO); glBindVertexArray(floorVAO);
        glBindBuffer(GL_ARRAY_BUFFER, floorVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(fv), fv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
        float dv[] = { -.05f,-.05f,0,.05f,-.05f,0,.05f,.05f,0,-.05f,-.05f,0,.05f,.05f,0,-.05f,.05f,0 };
        GLuint dvb; glGenVertexArrays(1, &dotVAO); glGenBuffers(1, &dvb); glBindVertexArray(dotVAO);
        glBindBuffer(GL_ARRAY_BUFFER, dvb); glBufferData(GL_ARRAY_BUFFER, sizeof(dv), dv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        wl.model = glGetUniformLocation(worldShader, "model"); wl.view = glGetUniformLocation(worldShader, "view");
        wl.proj = glGetUniformLocation(worldShader, "projection"); wl.nm = glGetUniformLocation(worldShader, "normalMatrix");
        wl.ld = glGetUniformLocation(worldShader, "lightDir"); wl.bc = glGetUniformLocation(worldShader, "baseColor");
        wl.ht = glGetUniformLocation(worldShader, "hasTexture"); wl.tex = glGetUniformLocation(worldShader, "tex");
        wl.fc = glGetUniformLocation(worldShader, "fogColor"); wl.fs = glGetUniformLocation(worldShader, "fogStart");
        wl.fe = glGetUniformLocation(worldShader, "fogEnd"); wl.cp = glGetUniformLocation(worldShader, "camPos");
        gl.model = glGetUniformLocation(gunShader, "model"); gl.view = glGetUniformLocation(gunShader, "view");
        gl.proj = glGetUniformLocation(gunShader, "projection"); gl.nm = glGetUniformLocation(gunShader, "normalMatrix");
        gl.sk = glGetUniformLocation(gunShader, "skinned"); gl.bones = glGetUniformLocation(gunShader, "bones");
        gl.ht = glGetUniformLocation(gunShader, "hasTexture"); gl.tex = glGetUniformLocation(gunShader, "tex");
        gl.gc = glGetUniformLocation(gunShader, "gunColor"); gl.flash = glGetUniformLocation(gunShader, "flash");
        dl.mvp = glGetUniformLocation(dotShader, "mvp"); dl.color = glGetUniformLocation(dotShader, "color");
    }

    void _glScene(const std::vector<GPUMesh>& mm, AnimatedModel& gm, glm::mat4 mapT, const glm::vec3& cam,
        const glm::mat4& view, const glm::mat4& proj, const glm::mat4& projG, const glm::mat4& gMat)
    {
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1, 1);
        glUseProgram(worldShader);
        glUniformMatrix4fv(wl.view, 1, GL_FALSE, glm::value_ptr(view)); glUniformMatrix4fv(wl.proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(glGetUniformLocation(worldShader, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        glUniform3fv(wl.ld, 1, glm::value_ptr(sunDir)); glUniform3fv(wl.cp, 1, glm::value_ptr(cam));
        glUniform3f(wl.fc, 0.68f, 0.65f, 0.60f); glUniform1f(wl.fs, 15.f); glUniform1f(wl.fe, 60.f);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shadowTex); glUniform1i(glGetUniformLocation(worldShader, "shadowMap"), 1); glActiveTexture(GL_TEXTURE0);
        if (!mm.empty()) {
            glUniformMatrix4fv(wl.model, 1, GL_FALSE, glm::value_ptr(mapT));
            glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(mapT))); glUniformMatrix3fv(wl.nm, 1, GL_FALSE, glm::value_ptr(nm));
            glUniform3f(wl.bc, .75f, .72f, .65f); GLuint lt = 0;
            for (auto& m : mm) { if (m.texID) { if (m.texID != lt) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.texID); lt = m.texID; }glUniform1i(wl.ht, 1); glUniform1i(wl.tex, 0); } else { glUniform1i(wl.ht, 0); lt = 0; }glBindVertexArray(m.VAO); glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0); }
        }
        else {
            glm::mat4 id(1.f); glUniformMatrix4fv(wl.model, 1, GL_FALSE, glm::value_ptr(id)); glUniform3f(wl.bc, .3f, .55f, .3f); glUniform1i(wl.ht, 0); glBindVertexArray(floorVAO); glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        if (!bulletHoles.empty()) { glUseProgram(dotShader); glBindVertexArray(dotVAO); for (auto& bh : bulletHoles) { glm::mat4 mvp = proj * view * glm::translate(glm::mat4(1.f), bh.pos); glUniformMatrix4fv(dl.mvp, 1, GL_FALSE, glm::value_ptr(mvp)); glUniform4f(dl.color, .05f, .05f, .05f, bh.life / 5.f); glDrawArrays(GL_TRIANGLES, 0, 6); } }
        if (!gm.meshes.empty()) {
            glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_CULL_FACE); glUseProgram(gunShader);
            glUniformMatrix4fv(gl.model, 1, GL_FALSE, glm::value_ptr(gMat)); glUniformMatrix4fv(gl.view, 1, GL_FALSE, glm::value_ptr(view)); glUniformMatrix4fv(gl.proj, 1, GL_FALSE, glm::value_ptr(projG));
            glm::mat3 nmg = glm::mat3(glm::transpose(glm::inverse(gMat))); glUniformMatrix3fv(gl.nm, 1, GL_FALSE, glm::value_ptr(nmg));
            glUniform3f(gl.gc, .4f, .4f, .42f); glUniform1f(gl.flash, flashTimer > 0.f ? flashTimer * 4.f : 0.f);
            bool hb = !gm.boneFinal.empty() && gm.boneCount > 0; glUniform1i(gl.sk, (int)hb);
            if (hb && gl.bones >= 0) { int bc = std::min(gm.boneCount, 100); glUniformMatrix4fv(gl.bones, bc, GL_FALSE, glm::value_ptr(gm.boneFinal[0])); }
            GLuint lt = 0; for (auto& m : gm.meshes) { if (m.texID) { if (m.texID != lt) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.texID); lt = m.texID; }glUniform1i(gl.ht, 1); glUniform1i(gl.tex, 0); } else { glUniform1i(gl.ht, 0); lt = 0; }glBindVertexArray(m.VAO); glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0); }
        }
    }

#ifdef _WIN32
    void _dxScene(const std::vector<GPUMesh>& mm, AnimatedModel& gm, glm::mat4 mapT, const glm::vec3& cam,
        const glm::mat4& view, const glm::mat4& proj, const glm::mat4& projG, const glm::mat4& gMat)
    {
        // Выбираем rasterizer state с учётом wireframe
        ID3D11RasterizerState* rs = dx11.wireframe ? dx11.rsWireframe.Get() : dx11.rsNorm.Get();

        DX_WF wf; wf.view = view; wf.proj = proj; wf.ls = lightSpaceMatrix; wf.ld = sunDir; wf.fc = glm::vec3(0.68f, 0.65f, 0.60f); wf.fs = 15.f; wf.fe = 60.f; wf.cp = cam;
        _dxUp(dx11.cbWF, &wf, sizeof(wf));
        dx11.ctx->VSSetShader(dx11.world.vs.Get(), nullptr, 0); dx11.ctx->PSSetShader(dx11.world.ps.Get(), nullptr, 0);
        dx11.ctx->IASetInputLayout(dx11.world.il.Get()); dx11.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dx11.ctx->RSSetState(rs);
        dx11.ctx->OMSetDepthStencilState(dx11.dssOn.Get(), 0); dx11.ctx->OMSetBlendState(dx11.bsOpaque.Get(), nullptr, 0xFFFFFFFF);
        dx11.ctx->VSSetConstantBuffers(0, 1, dx11.cbWF.GetAddressOf()); dx11.ctx->PSSetConstantBuffers(0, 1, dx11.cbWF.GetAddressOf());
        dx11.ctx->PSSetSamplers(0, 1, dx11.sampLin.GetAddressOf());
        if (!mm.empty()) {
            for (auto& m : mm) {
                DX_WO wo; wo.model = mapT; wo.nm = glm::mat4(glm::transpose(glm::inverse(glm::mat3(mapT)))); wo.bc = glm::vec3(.75f, .72f, .65f); wo.ht = 0;
                if (m.dxSRV) { ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(m.dxSRV); dx11.ctx->PSSetShaderResources(0, 1, &srv); wo.ht = 1; }
                else { ID3D11ShaderResourceView* null = nullptr; dx11.ctx->PSSetShaderResources(0, 1, &null); wo.ht = 0; }
                _dxUp(dx11.cbWO, &wo, sizeof(wo)); dx11.ctx->VSSetConstantBuffers(1, 1, dx11.cbWO.GetAddressOf()); dx11.ctx->PSSetConstantBuffers(1, 1, dx11.cbWO.GetAddressOf());
                _dxMesh(m);
            }
        }
        if (!gm.meshes.empty()) {
            dx11.ctx->ClearDepthStencilView(dx11.postDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            dx11.ctx->RSSetState(dx11.rsNoCull.Get()); // оружие — без culling
            dx11.ctx->VSSetShader(dx11.gun.vs.Get(), nullptr, 0); dx11.ctx->PSSetShader(dx11.gun.ps.Get(), nullptr, 0); dx11.ctx->IASetInputLayout(dx11.gun.il.Get());
            DX_GF gf; gf.view = view; gf.proj = projG; _dxUp(dx11.cbGF, &gf, sizeof(gf));
            DX_GO go; go.model = gMat; go.nm = glm::mat4(glm::transpose(glm::inverse(glm::mat3(gMat)))); go.gc = glm::vec3(.4f, .4f, .42f); go.flash = flashTimer > 0.f ? flashTimer * 4.f : 0.f; go.ht = 0;
            bool hb = !gm.boneFinal.empty() && gm.boneCount > 0; go.sk = hb ? 1 : 0;
            if (hb) { int bc = std::min(gm.boneCount, 100); _dxUp(dx11.cbBones, glm::value_ptr(gm.boneFinal[0]), (size_t)bc * 64); dx11.ctx->VSSetConstantBuffers(2, 1, dx11.cbBones.GetAddressOf()); }
            _dxUp(dx11.cbGF, &gf, sizeof(gf)); _dxUp(dx11.cbGO, &go, sizeof(go));
            dx11.ctx->VSSetConstantBuffers(0, 1, dx11.cbGF.GetAddressOf()); dx11.ctx->VSSetConstantBuffers(1, 1, dx11.cbGO.GetAddressOf());
            dx11.ctx->PSSetConstantBuffers(0, 1, dx11.cbGF.GetAddressOf()); dx11.ctx->PSSetConstantBuffers(1, 1, dx11.cbGO.GetAddressOf());
            for (auto& m : gm.meshes) {
                if (m.dxSRV) { ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(m.dxSRV); dx11.ctx->PSSetShaderResources(0, 1, &srv); go.ht = 1; }
                else { ID3D11ShaderResourceView* null = nullptr; dx11.ctx->PSSetShaderResources(0, 1, &null); go.ht = 0; }
                _dxUp(dx11.cbGO, &go, sizeof(go)); dx11.ctx->PSSetConstantBuffers(1, 1, dx11.cbGO.GetAddressOf());
                _dxMesh(m);
            }
        }
    }
#endif
};