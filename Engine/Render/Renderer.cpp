// ============================================================
//  Engine/Render/Renderer.cpp  —  DirectX 11
// ============================================================

#include "Renderer.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char* g_vsSource = R"HLSL(
cbuffer ModelCB : register(b0) { matrix mWorld; };
cbuffer FrameCB : register(b1) {
    matrix mView; matrix mProj;
    float3 lightDir; float lightIntensity;
    float3 viewPos;  float _pad;
};
cbuffer BoneCB : register(b2) { matrix bones[100]; };

struct VSIn {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float3 tangent : TANGENT;
    int4   boneIDs : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};
struct VSOut {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

VSOut main(VSIn IN) {
    float4 skinnedPos = float4(0,0,0,0);
    float3 skinnedNrm = float3(0,0,0);
    [unroll] for (int i = 0; i < 4; i++) {
        int bi = IN.boneIDs[i];
        if (bi < 0) continue;
        skinnedPos += IN.weights[i] * mul(float4(IN.pos,1.0), bones[bi]);
        skinnedNrm += IN.weights[i] * mul(IN.normal, (float3x3)bones[bi]);
    }
    float w = IN.weights[0]+IN.weights[1]+IN.weights[2]+IN.weights[3];
    if (w < 0.001f) { skinnedPos = float4(IN.pos,1.0); skinnedNrm = IN.normal; }
    float4 worldPos = mul(skinnedPos, mWorld);
    VSOut OUT;
    OUT.pos      = mul(mul(worldPos, mView), mProj);
    OUT.worldPos = worldPos.xyz;
    OUT.normal   = normalize(mul(skinnedNrm, (float3x3)mWorld));
    OUT.uv       = IN.uv;
    return OUT;
}
)HLSL";

static const char* g_psSource = R"HLSL(
cbuffer FrameCB : register(b1) {
    matrix mView; matrix mProj;
    float3 lightDir; float lightIntensity;
    float3 viewPos;  float _pad;
};
Texture2D diffuseTex : register(t0);
SamplerState samLinear : register(s0);

struct PSIn {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

float4 main(PSIn IN) : SV_TARGET {
    float3 base  = diffuseTex.Sample(samLinear, IN.uv).rgb;
    float3 n     = normalize(IN.normal);
    float3 l     = normalize(-lightDir);
    float  diff  = max(dot(n,l), 0.0f) * lightIntensity;
    float3 col   = base * (float3(0.15f,0.15f,0.15f) + diff);
    float  dist  = length(viewPos - IN.worldPos);
    float  fog   = saturate((dist - 30.0f) / 70.0f);
    col          = lerp(col, float3(0.5f,0.5f,0.5f), fog);
    return float4(col, 1.0f);
}
)HLSL";

Renderer::Renderer() : m_width(0), m_height(0) {}
Renderer::~Renderer() { Cleanup(); }

bool Renderer::Initialize(HWND hWnd, int width, int height) {
    m_width = width; m_height = height;
    if (!CreateDeviceAndSwapChain(hWnd)) return false;
    if (!CreateRenderViews())            return false;
    if (!CreateShaders())                return false;
    if (!CreateConstantBuffers())        return false;
    D3D11_VIEWPORT vp{};
    vp.Width = (FLOAT)width; vp.Height = (FLOAT)height;
    vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
    m_pImmediateContext->RSSetViewports(1, &vp);
    return true;
}

void Renderer::Cleanup() {
    if (m_pImmediateContext) m_pImmediateContext->ClearState();
}

void Renderer::BeginFrame() {
    float c[] = { 0.1f, 0.1f, 0.1f, 1.f };
    m_pImmediateContext->ClearRenderTargetView(m_pRenderTargetView.Get(), c);
    m_pImmediateContext->ClearDepthStencilView(m_pDepthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    m_pImmediateContext->OMSetRenderTargets(1,
        m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
}

void Renderer::EndFrame() { m_pSwapChain->Present(1, 0); }

void Renderer::SetFrameData(const XMMATRIX& view, const XMMATRIX& proj,
    const XMFLOAT3& viewPos, const XMFLOAT3& lightDir, float lightIntensity) {
    FrameConstantBuffer fcb;
    fcb.mView = XMMatrixTranspose(view);
    fcb.mProj = XMMatrixTranspose(proj);
    fcb.viewPos = viewPos; fcb.lightDir = lightDir;
    fcb.lightIntensity = lightIntensity; fcb._pad = 0.f;
    m_pImmediateContext->UpdateSubresource(m_pFrameConstantBuffer.Get(), 0, nullptr, &fcb, 0, 0);
    m_pImmediateContext->VSSetConstantBuffers(1, 1, m_pFrameConstantBuffer.GetAddressOf());
    m_pImmediateContext->PSSetConstantBuffers(1, 1, m_pFrameConstantBuffer.GetAddressOf());
}

void Renderer::RenderObject(const RenderObject& obj,
    const XMMATRIX& view, const XMMATRIX& proj) {
    if (!obj.mesh) return;

    ModelConstantBuffer mcb;
    mcb.mWorld = XMMatrixTranspose(obj.worldMatrix);
    m_pImmediateContext->UpdateSubresource(m_pModelConstantBuffer.Get(), 0, nullptr, &mcb, 0, 0);
    m_pImmediateContext->VSSetConstantBuffers(0, 1, m_pModelConstantBuffer.GetAddressOf());

    BoneConstantBuffer bcb;
    for (int i = 0; i < 100; ++i) bcb.bones[i] = XMMatrixIdentity();
    if (obj.bones && obj.boneCount > 0)
        for (int i = 0; i < obj.boneCount && i < 100; ++i)
            bcb.bones[i] = XMMatrixTranspose(obj.bones[i]);
    m_pImmediateContext->UpdateSubresource(m_pBoneConstantBuffer.Get(), 0, nullptr, &bcb, 0, 0);
    m_pImmediateContext->VSSetConstantBuffers(2, 1, m_pBoneConstantBuffer.GetAddressOf());

    RenderMesh(obj.mesh, obj.worldMatrix, view, proj);
}

void Renderer::RenderMesh(Mesh* mesh, const XMMATRIX&, const XMMATRIX&, const XMMATRIX&) {
    if (!mesh) return;
    UINT stride = sizeof(Vertex), offset = 0;
    m_pImmediateContext->IASetVertexBuffers(0, 1, mesh->GetVertexBufferAddress(), &stride, &offset);
    m_pImmediateContext->IASetIndexBuffer(mesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0);
    m_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pImmediateContext->DrawIndexed(mesh->GetIndexCount(), 0, 0);
}

bool Renderer::CreateDeviceAndSwapChain(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = m_width; sd.BufferDesc.Height = m_height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL outLevel;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        fl, 1, D3D11_SDK_VERSION,
        &sd, &m_pSwapChain, &m_pDevice, &outLevel, &m_pImmediateContext));
}

bool Renderer::CreateRenderViews() {
    ComPtr<ID3D11Texture2D> pBack;
    if (FAILED(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(pBack.GetAddressOf())))) return false;
    if (FAILED(m_pDevice->CreateRenderTargetView(pBack.Get(), nullptr, &m_pRenderTargetView)))
        return false;
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = m_width; dd.Height = m_height;
    dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(m_pDevice->CreateTexture2D(&dd, nullptr, &m_pDepthStencilBuffer))) return false;
    return SUCCEEDED(m_pDevice->CreateDepthStencilView(
        m_pDepthStencilBuffer.Get(), nullptr, &m_pDepthStencilView));
}

bool Renderer::CreateShaders() {
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    if (FAILED(D3DCompile(g_vsSource, strlen(g_vsSource), nullptr, nullptr, nullptr,
        "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errBlob)))
        return false;
    if (FAILED(m_pDevice->CreateVertexShader(vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), nullptr, &m_pVertexShader))) return false;
    if (FAILED(D3DCompile(g_psSource, strlen(g_psSource), nullptr, nullptr, nullptr,
        "main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &psBlob, &errBlob)))
        return false;
    if (FAILED(m_pDevice->CreatePixelShader(psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(), nullptr, &m_pPixelShader))) return false;
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT,  0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(m_pDevice->CreateInputLayout(layout, ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_pInputLayout))) return false;
    m_pImmediateContext->IASetInputLayout(m_pInputLayout.Get());
    m_pImmediateContext->VSSetShader(m_pVertexShader.Get(), nullptr, 0);
    m_pImmediateContext->PSSetShader(m_pPixelShader.Get(), nullptr, 0);
    return true;
}

bool Renderer::CreateConstantBuffers() {
    auto make = [&](UINT size, ComPtr<ID3D11Buffer>& buf) -> bool {
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = size;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(m_pDevice->CreateBuffer(&bd, nullptr, &buf));
        };
    return make(sizeof(ModelConstantBuffer), m_pModelConstantBuffer)
        && make(sizeof(BoneConstantBuffer), m_pBoneConstantBuffer)
        && make(sizeof(FrameConstantBuffer), m_pFrameConstantBuffer);
}