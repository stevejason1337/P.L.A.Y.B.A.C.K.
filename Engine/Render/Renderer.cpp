#include "Renderer.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// НИКАКИХ Player.h / Enemy.h / WeaponManager.h здесь!

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

void Renderer::BeginFrame() {
    float c[] = { 0.1f, 0.1f, 0.1f, 1.f };
    m_pImmediateContext->ClearRenderTargetView(m_pRenderTargetView.Get(), c);
    m_pImmediateContext->ClearDepthStencilView(m_pDepthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    m_pImmediateContext->OMSetRenderTargets(1,
        m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
}

void Renderer::EndFrame() { m_pSwapChain->Present(1, 0); }

// Один универсальный метод — игра сама готовит RenderObject
void Renderer::RenderObject(const RenderObject& obj,
    const XMMATRIX& view,
    const XMMATRIX& proj)
{
    if (!obj.mesh) return;

    // Обновляем world матрицу
    ModelConstantBuffer mcb;
    mcb.mWorld = XMMatrixTranspose(obj.worldMatrix);
    m_pImmediateContext->UpdateSubresource(
        m_pModelConstantBuffer.Get(), 0, nullptr, &mcb, 0, 0);

    // Если есть кости — обновляем bone buffer
    if (obj.bones && obj.boneCount > 0) {
        BoneConstantBuffer bcb;
        for (int i = 0; i < obj.boneCount && i < 100; ++i)
            bcb.bones[i] = XMMatrixTranspose(obj.bones[i]);
        m_pImmediateContext->UpdateSubresource(
            m_pBoneConstantBuffer.Get(), 0, nullptr, &bcb, 0, 0);
    }

    RenderMesh(obj.mesh, obj.worldMatrix, view, proj);
}

void Renderer::RenderMesh(Mesh* mesh, const XMMATRIX&, const XMMATRIX&, const XMMATRIX&) {
    if (!mesh) return;
    UINT stride = sizeof(Vertex), offset = 0;
    m_pImmediateContext->IASetVertexBuffers(0, 1,
        mesh->GetVertexBufferAddress(), &stride, &offset);
    m_pImmediateContext->IASetIndexBuffer(
        mesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0);
    m_pImmediateContext->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pImmediateContext->DrawIndexed(mesh->GetIndexCount(), 0, 0);
}

// ... остальные методы (CreateDeviceAndSwapChain и т.д.) без изменений