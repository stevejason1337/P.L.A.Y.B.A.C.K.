#pragma once
// TAA.h — Temporal Anti-Aliasing (DX11 only, OpenGL stub)
// gTAAEnabled — глобальный флаг, меняется из меню настроек
#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif
#include <glm/glm.hpp>
#include "Settings.h"

inline bool gTAAEnabled = true;

#ifdef _WIN32
static const char* HLSL_TAA_SRC = R"(
cbuffer CB:register(b0){float2 resolution;float blend;float _pad;};
Texture2D curTex:register(t0);Texture2D hisTex:register(t1);
SamplerState sPoint:register(s0);SamplerState sLin:register(s1);
struct V2P{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V2P VSMain(uint id:SV_VertexID){
    float2 p[6]={float2(-1,1),float2(1,1),float2(1,-1),float2(-1,1),float2(1,-1),float2(-1,-1)};
    float2 u[6]={float2(0,0),float2(1,0),float2(1,1),float2(0,0),float2(1,1),float2(0,1)};
    V2P o;o.pos=float4(p[id],0,1);o.uv=u[id];return o;
}
float3 ClipHistory(float3 his,float3 mn,float3 mx){
    float3 center=(mn+mx)*0.5;float3 ext=(mx-mn)*0.5+0.001;
    float3 v=his-center;float3 a=abs(v/ext);
    float m=max(a.x,max(a.y,a.z));if(m>1.0)return center+v/m;return his;
}
float4 PSMain(V2P i):SV_Target{
    float2 uv=i.uv,px=1.0/resolution;
    float3 cur=curTex.Sample(sPoint,uv).rgb;
    float3 mn=cur,mx=cur;
    [unroll]for(int dy=-1;dy<=1;dy++){[unroll]for(int dx=-1;dx<=1;dx++){
        float3 s=curTex.Sample(sPoint,uv+float2(dx,dy)*px).rgb;mn=min(mn,s);mx=max(mx,s);}}
    float3 his=ClipHistory(hisTex.Sample(sLin,uv).rgb,mn,mx);
    return float4(lerp(cur,his,blend),1.0);
}
)";

struct TAASystem {
    ComPtr<ID3D11Device>        dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11Texture2D>          bufA, bufB;
    ComPtr<ID3D11RenderTargetView>   rtvA, rtvB;
    ComPtr<ID3D11ShaderResourceView> srvA, srvB;
    ComPtr<ID3D11VertexShader>  vs;
    ComPtr<ID3D11PixelShader>   ps;
    ComPtr<ID3D11Buffer>        cb;
    ComPtr<ID3D11SamplerState>  sampPoint, sampLin;
    bool ping = true, firstFrame = true, ready = false;
    int  frameIndex = 0;

    void init(ID3D11Device* d, ID3D11DeviceContext* c, int w, int h) {
        dev = d; ctx = c; _createBuffers(w, h); _compileShader(); _createSamplers(); _createCB();
        firstFrame = true; ping = true; ready = true; printf("[TAA] Init %dx%d\n", w, h);
    }
    void resize(int w, int h) {
        if (!ready)return;
        rtvA.Reset(); srvA.Reset(); bufA.Reset(); rtvB.Reset(); srvB.Reset(); bufB.Reset();
        _createBuffers(w, h); firstFrame = true;
    }
    void applyJitter(glm::mat4& proj) {
        if (!gTAAEnabled)return;
        frameIndex = (frameIndex + 1) % 16;
        float jx = (_halton(frameIndex + 1, 2) - 0.5f) * 2.f / (float)SCR_WIDTH;
        float jy = (_halton(frameIndex + 1, 3) - 0.5f) * 2.f / (float)SCR_HEIGHT;
        proj[2][0] += jx; proj[2][1] += jy;
    }
    ID3D11ShaderResourceView* resolve(ID3D11ShaderResourceView* curSRV) {
        if (!ready || !gTAAEnabled)return curSRV;
        ID3D11RenderTargetView* dstRTV = ping ? rtvA.Get() : rtvB.Get();
        ID3D11ShaderResourceView* hisSRV = ping ? srvB.Get() : srvA.Get();
        ID3D11ShaderResourceView* outSRV = ping ? srvA.Get() : srvB.Get();
        struct CB { float res[2]; float blend; float pad; };
        CB data; data.res[0] = (float)SCR_WIDTH; data.res[1] = (float)SCR_HEIGHT;
        data.blend = firstFrame ? 0.f : 0.9f; firstFrame = false;
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, &data, sizeof(data)); ctx->Unmap(cb.Get(), 0); }
        ctx->OMSetRenderTargets(1, &dstRTV, nullptr);
        ctx->VSSetShader(vs.Get(), nullptr, 0); ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ID3D11ShaderResourceView* srvs[2] = { curSRV,hisSRV };
        ID3D11SamplerState* smps[2] = { sampPoint.Get(),sampLin.Get() };
        ctx->PSSetShaderResources(0, 2, srvs); ctx->PSSetSamplers(0, 2, smps);
        ctx->Draw(6, 0);
        ID3D11ShaderResourceView* nulls[2] = { nullptr,nullptr }; ctx->PSSetShaderResources(0, 2, nulls);
        ping = !ping; return outSRV;
    }
private:
    float _halton(int i, int base) { float f = 1.f, r = 0.f; while (i > 0) { f /= base; r += f * (i % base); i /= base; }return r; }
    void _createBuffers(int w, int h) {
        D3D11_TEXTURE2D_DESC td = {}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        dev->CreateTexture2D(&td, nullptr, &bufA); dev->CreateRenderTargetView(bufA.Get(), nullptr, &rtvA); dev->CreateShaderResourceView(bufA.Get(), nullptr, &srvA);
        dev->CreateTexture2D(&td, nullptr, &bufB); dev->CreateRenderTargetView(bufB.Get(), nullptr, &rtvB); dev->CreateShaderResourceView(bufB.Get(), nullptr, &srvB);
    }
    void _compileShader() {
        ComPtr<ID3DBlob>vsb, psb, err;
        D3DCompile(HLSL_TAA_SRC, strlen(HLSL_TAA_SRC), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &err);
        if (vsb)dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        D3DCompile(HLSL_TAA_SRC, strlen(HLSL_TAA_SRC), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &err);
        if (psb)dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    }
    void _createSamplers() {
        D3D11_SAMPLER_DESC sd = {}; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; dev->CreateSamplerState(&sd, &sampPoint);
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; dev->CreateSamplerState(&sd, &sampLin);
    }
    void _createCB() {
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&bd, nullptr, &cb);
    }
};
inline TAASystem gTAA;
#else
inline bool gTAAEnabled = true;
struct TAASystem {
    bool ready = false;
    void init(void*, void*, int, int) {}
    void resize(int, int) {}
    void applyJitter(glm::mat4&) {}
    void* resolve(void* srv) { return srv; }
};
inline TAASystem gTAA;
#endif