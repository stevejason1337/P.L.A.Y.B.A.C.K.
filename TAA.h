#pragma once
// ═══════════════════════════════════════════════════════════════
//  TAA.h  —  Temporal Anti-Aliasing для DX11
//
//  Использование:
//    #include "TAA.h"
//
//    // В init после renderer.init():
//    gTAA.init(dx11Device, dx11Context, SCR_WIDTH, SCR_HEIGHT);
//
//    // В drawScene, после построения proj матрицы:
//    gTAA.applyJitter(proj);
//
//    // В endFrame, между сценой и post-process:
//    gTAA.resolve(postSRV, taaSRV_out);  // возвращает SRV с TAA результатом
//
//    // При resize:
//    gTAA.resize(newW, newH);
// ═══════════════════════════════════════════════════════════════

#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#include <glm/glm.hpp>
#include "Settings.h"

// ─── Глобальный флаг — включён ли TAA (меняется из меню) ─────
inline bool gTAAEnabled = true;

#ifdef _WIN32

// ─────────────────────────────────────────────────────────────
//  HLSL шейдер TAA resolve
//  t0 = текущий кадр (postTex)
//  t1 = история (предыдущий TAA результат)
// ─────────────────────────────────────────────────────────────
static const char* HLSL_TAA_SRC = R"(
cbuffer CB : register(b0) {
    float2 resolution;
    float  blend;      // 0.0 = нет истории (первый кадр), 0.9 = обычно
    float  _pad;
};
Texture2D   curTex  : register(t0);   // текущий кадр
Texture2D   hisTex  : register(t1);   // история
SamplerState sPoint : register(s0);   // point — для текущего кадра
SamplerState sLin   : register(s1);   // linear — для истории

struct V2P { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

V2P VSMain(uint id : SV_VertexID) {
    float2 pos[6] = { float2(-1,1),float2(1,1),float2(1,-1),
                      float2(-1,1),float2(1,-1),float2(-1,-1) };
    float2 uvs[6] = { float2(0,0),float2(1,0),float2(1,1),
                      float2(0,0),float2(1,1),float2(0,1) };
    V2P o; o.pos = float4(pos[id], 0, 1); o.uv = uvs[id]; return o;
}

// Variance clipping — зажимаем историю в bounding box соседей текущего кадра
// Убирает ghosting (тянущиеся тени за движущимися объектами)
float3 ClipHistory(float3 his, float3 minC, float3 maxC) {
    float3 center = (minC + maxC) * 0.5;
    float3 extents = (maxC - minC) * 0.5 + 0.001;
    float3 v = his - center;
    float3 a = abs(v / extents);
    float maxA = max(a.x, max(a.y, a.z));
    if (maxA > 1.0) return center + v / maxA;
    return his;
}

float4 PSMain(V2P i) : SV_Target {
    float2 uv = i.uv;
    float2 px = 1.0 / resolution;

    float3 cur = curTex.Sample(sPoint, uv).rgb;

    // Собираем 3x3 соседей для variance clipping
    float3 minC = cur, maxC = cur;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
    [unroll] for (int dx = -1; dx <= 1; dx++) {
        float3 s = curTex.Sample(sPoint, uv + float2(dx, dy) * px).rgb;
        minC = min(minC, s);
        maxC = max(maxC, s);
    }}

    // Берём историю и зажимаем её
    float3 his = hisTex.Sample(sLin, uv).rgb;
    his = ClipHistory(his, minC, maxC);

    // Смешиваем: больше истории = мягче, но потенциально ghosting
    float3 col = lerp(cur, his, blend);
    return float4(col, 1.0);
}
)";

// ═══════════════════════════════════════════════════════════════
struct TAASystem {

    // DX11 ресурсы
    ComPtr<ID3D11Device>        dev;
    ComPtr<ID3D11DeviceContext> ctx;

    // Ping-pong буферы истории (R16G16B16A16 — больше точности чем R8)
    ComPtr<ID3D11Texture2D>          bufA, bufB;
    ComPtr<ID3D11RenderTargetView>   rtvA, rtvB;
    ComPtr<ID3D11ShaderResourceView> srvA, srvB;

    // Шейдер и сэмплеры
    ComPtr<ID3D11VertexShader>  vs;
    ComPtr<ID3D11PixelShader>   ps;
    ComPtr<ID3D11Buffer>        cb;
    ComPtr<ID3D11SamplerState>  sampPoint, sampLin;

    bool ping        = true;   // true = пишем в A, читаем из B
    bool firstFrame  = true;
    int  frameIndex  = 0;
    bool ready       = false;

    // ─────────────────────────────────────────────────────────
    void init(ID3D11Device* d, ID3D11DeviceContext* c, int w, int h) {
        dev = d; ctx = c;
        _createBuffers(w, h);
        _compileShader();
        _createSamplers();
        _createCB();
        firstFrame = true;
        ping = true;
        ready = true;
        printf("[TAA] Initialized %dx%d\n", w, h);
    }

    // ─────────────────────────────────────────────────────────
    void resize(int w, int h) {
        if (!ready) return;
        rtvA.Reset(); srvA.Reset(); bufA.Reset();
        rtvB.Reset(); srvB.Reset(); bufB.Reset();
        _createBuffers(w, h);
        firstFrame = true;
        printf("[TAA] Resized to %dx%d\n", w, h);
    }

    // ─────────────────────────────────────────────────────────
    //  Применяет Halton sub-pixel jitter к матрице проекции
    //  Вызывай ПОСЛЕ glm::perspective(), ДО передачи в шейдер
    // ─────────────────────────────────────────────────────────
    void applyJitter(glm::mat4& proj) {
        if (!gTAAEnabled) return;
        frameIndex = (frameIndex + 1) % 16;
        float jx = (_halton(frameIndex + 1, 2) - 0.5f) * 2.f / (float)SCR_WIDTH;
        float jy = (_halton(frameIndex + 1, 3) - 0.5f) * 2.f / (float)SCR_HEIGHT;
        // Смещаем X и Y в clip space (column-major glm)
        proj[2][0] += jx;
        proj[2][1] += jy;
    }

    // ─────────────────────────────────────────────────────────
    //  Выполняет TAA resolve
    //  curSRV  — SRV текущего кадра (postTex)
    //  Возвращает SRV с результатом TAA (для передачи в post-шейдер)
    // ─────────────────────────────────────────────────────────
    ID3D11ShaderResourceView* resolve(ID3D11ShaderResourceView* curSRV) {
        if (!ready || !gTAAEnabled) return curSRV;  // TAA выключен — пропускаем

        // Текущий буфер куда пишем, история откуда читаем
        ID3D11RenderTargetView*   dstRTV = ping ? rtvA.Get() : rtvB.Get();
        ID3D11ShaderResourceView* hisSRV = ping ? srvB.Get() : srvA.Get();
        ID3D11ShaderResourceView* outSRV = ping ? srvA.Get() : srvB.Get();

        // Обновляем constant buffer
        struct CB { float res[2]; float blend; float pad; };
        CB data;
        data.res[0] = (float)SCR_WIDTH;
        data.res[1] = (float)SCR_HEIGHT;
        data.blend  = firstFrame ? 0.0f : 0.9f;
        data.pad    = 0.f;
        firstFrame  = false;

        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            memcpy(ms.pData, &data, sizeof(data));
            ctx->Unmap(cb.Get(), 0);
        }

        // Рисуем полноэкранный треугольник
        ctx->OMSetRenderTargets(1, &dstRTV, nullptr);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());

        ID3D11ShaderResourceView* srvs[2] = { curSRV, hisSRV };
        ID3D11SamplerState*       smps[2] = { sampPoint.Get(), sampLin.Get() };
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->PSSetSamplers(0, 2, smps);
        ctx->Draw(6, 0);

        // Очищаем слоты
        ID3D11ShaderResourceView* nulls[2] = { nullptr, nullptr };
        ctx->PSSetShaderResources(0, 2, nulls);

        // Следующий кадр меняем ping/pong
        ping = !ping;
        return outSRV;
    }

private:
    // ─────────────────────────────────────────────────────────
    float _halton(int i, int base) {
        float f = 1.f, r = 0.f;
        while (i > 0) { f /= base; r += f * (i % base); i /= base; }
        return r;
    }

    void _createBuffers(int w, int h) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // fp16 — меньше banding
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        dev->CreateTexture2D(&td, nullptr, &bufA);
        dev->CreateRenderTargetView(bufA.Get(), nullptr, &rtvA);
        dev->CreateShaderResourceView(bufA.Get(), nullptr, &srvA);

        dev->CreateTexture2D(&td, nullptr, &bufB);
        dev->CreateRenderTargetView(bufB.Get(), nullptr, &rtvB);
        dev->CreateShaderResourceView(bufB.Get(), nullptr, &srvB);
    }

    void _compileShader() {
        ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

        HRESULT hr = D3DCompile(HLSL_TAA_SRC, strlen(HLSL_TAA_SRC),
            nullptr, nullptr, nullptr, "VSMain", "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) printf("[TAA] VS error: %s\n", (char*)errBlob->GetBufferPointer());
            return;
        }
        dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);

        hr = D3DCompile(HLSL_TAA_SRC, strlen(HLSL_TAA_SRC),
            nullptr, nullptr, nullptr, "PSMain", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) printf("[TAA] PS error: %s\n", (char*)errBlob->GetBufferPointer());
            return;
        }
        dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
    }

    void _createSamplers() {
        D3D11_SAMPLER_DESC sd = {};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        dev->CreateSamplerState(&sd, &sampPoint);

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        dev->CreateSamplerState(&sd, &sampLin);
    }

    void _createCB() {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 16;  // float2 res + float blend + float pad
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&bd, nullptr, &cb);
    }
};

inline TAASystem gTAA;

#else
// ─── Заглушка для не-Windows (OpenGL) ────────────────────────
inline bool gTAAEnabled = true;  // уже объявлен выше, но на всякий случай
struct TAASystem {
    bool ready = false;
    void init(void*, void*, int, int) {}
    void resize(int, int) {}
    void applyJitter(glm::mat4&) {}
    void* resolve(void* srv) { return srv; }
};
inline TAASystem gTAA;
#endif
