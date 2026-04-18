#include "Renderer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Settings.h"
#include "ShaderTranspiler.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>

// Вот здесь мы подключаем "игровое дерьмо", потому что в .cpp это можно!
// Это не создаст циклических зависимостей для других файлов.
#include "Player.h"
#include "WeaponManager.h"
#include "AnimatedModel.h"
#include "Enemy.h"

// Системные инклюды
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Вспомогательная функция для обновления буферов (из твоего старого кода)
void _dxUp(Microsoft::WRL::ComPtr<ID3D11Buffer> buf, const void* data, size_t sz, ID3D11DeviceContext* ctx) {
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx->Map(buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, data, sz);
        ctx->Unmap(buf.Get(), 0);
    }
}

Renderer::Renderer() : m_width(0), m_height(0) {}

Renderer::~Renderer() {
    Cleanup();
}

bool Renderer::Initialize(HWND hWnd, int width, int height) {
    m_width = width;
    m_height = height;

    if (!CreateDeviceAndSwapChain(hWnd)) return false;
    if (!CreateRenderViews()) return false;
    if (!CreateShaders()) return false;
    if (!CreateConstantBuffers()) return false;

    // Настройка вьюпорта
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_pImmediateContext->RSSetViewports(1, &vp);

    return true;
}

void Renderer::BeginFrame() {
    float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f }; // Темно-серый фон бункера
    m_pImmediateContext->ClearRenderTargetView(m_pRenderTargetView.Get(), clearColor);
    m_pImmediateContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_pImmediateContext->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
}

void Renderer::EndFrame() {
    m_pSwapChain->Present(1, 0);
}

void Renderer::RenderPlayer(Player* player, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix) {
    if (!player) return;
    // Здесь мы вызываем методы игрока, так как Player.h подключен выше
    XMMATRIX world = player->GetWorldMatrix();
    RenderAnimatedModel(player->GetModel(), world, viewMatrix, projectionMatrix);
}

void Renderer::RenderWeapon(WeaponManager* weapon, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix) {
    if (!weapon) return;
    XMMATRIX world = weapon->GetWeaponWorldMatrix();
    RenderMesh(weapon->GetMesh(), world, viewMatrix, projectionMatrix);
}

void Renderer::RenderAnimatedModel(AnimatedModel* model, const XMMATRIX& worldMatrix, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix) {
    if (!model) return;

    // 1. Обновляем константный буфер трансформации
    ModelConstantBuffer mcb;
    mcb.mWorld = XMMatrixTranspose(worldMatrix);
    m_pImmediateContext->UpdateSubresource(m_pModelConstantBuffer.Get(), 0, nullptr, &mcb, 0, 0);

    // 2. Если есть кости (анимация), обновляем костный буфер
    if (model->hasAnimations) {
        BoneConstantBuffer bcb;
        // Копируем матрицы костей из модели
        for (int i = 0; i < model->boneCount && i < 100; ++i) {
            bcb.bones[i] = XMMatrixTranspose(model->boneTransforms[i]);
        }
        m_pImmediateContext->UpdateSubresource(m_pBoneConstantBuffer.Get(), 0, nullptr, &bcb, 0, 0);
    }

    // 3. Отрисовка мешей модели
    for (auto& mesh : model->meshes) {
        RenderMesh(&mesh, worldMatrix, viewMatrix, projectionMatrix);
    }
}

void Renderer::RenderMesh(Mesh* mesh, const XMMATRIX& worldMatrix, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix) {
    if (!mesh) return;

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_pImmediateContext->IASetVertexBuffers(0, 1, mesh->GetVertexBufferAddress(), &stride, &offset);
    m_pImmediateContext->IASetIndexBuffer(mesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0);
    m_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_pImmediateContext->DrawIndexed(mesh->GetIndexCount(), 0, 0);
}

bool Renderer::CreateDeviceAndSwapChain(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = m_width;
    sd.BufferDesc.Height = m_height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, 1, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, nullptr, &m_pImmediateContext));
}

bool Renderer::CreateRenderViews() {
    ComPtr<ID3D11Texture2D> pBackBuffer;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    m_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &m_pRenderTargetView);

    D3D11_TEXTURE2D_DESC descDepth = {};
    descDepth.Width = m_width;
    descDepth.Height = m_height;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descDepth.SampleDesc.Count = 1;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    m_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil);
    m_pd3dDevice->CreateDepthStencilView(m_pDepthStencil.Get(), nullptr, &m_pDepthStencilView);

    return true;
}

bool Renderer::CreateShaders() {
    // Здесь должна быть логика загрузки твоих шейдеров. 
    // Если ты используешь ShaderTranspiller, вызывай его здесь.
    return true;
}

bool Renderer::CreateConstantBuffers() {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    bd.ByteWidth = sizeof(ConstantBuffer);
    m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pConstantBuffer);

    bd.ByteWidth = sizeof(ModelConstantBuffer);
    m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pModelConstantBuffer);

    bd.ByteWidth = sizeof(BoneConstantBuffer);
    m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pBoneConstantBuffer);

    return true;
}

void Renderer::Cleanup() {
    // ComPtr очищаются сами, но если есть сырые указатели - удаляй здесь
}