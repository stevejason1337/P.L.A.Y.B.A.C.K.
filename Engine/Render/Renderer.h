#pragma once

#include <d3d11.h>
#include <directxmath.h>
#include <vector>
#include <string>
#include <wrl/client.h>

// Подключаем только системные вещи и базовые типы движка
#include "DX11Core.h"
#include "Mesh.h"
#include "Camera.h"
#include "Light.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// --- FORWARD DECLARATIONS (Опережающие объявления) ---
// Это позволяет не подключать тяжелые файлы игры в движок
class Player;
class WeaponManager;
class AnimatedModel;
class Enemy;
class ShaderTranspiller;

struct ConstantBuffer {
    XMMATRIX mWorld;
    XMMATRIX mView;
    XMMATRIX mProjection;
    XMFLOAT4 vLightDir;
    XMFLOAT4 vLightColor;
    XMFLOAT4 vAmbientColor;
    XMFLOAT4 vCameraPos;
};

struct ModelConstantBuffer {
    XMMATRIX mWorld;
};

struct BoneConstantBuffer {
    XMMATRIX bones[100];
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hWnd, int width, int height);
    void BeginFrame();
    void EndFrame();

    // Методы отрисовки теперь принимают указатели. 
    // Реализация этих методов должна быть в Renderer.cpp, 
    // и именно в .cpp файле нужно будет заинклюдить "Player.h" и т.д.
    void RenderPlayer(Player* player, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);
    void RenderWeapon(WeaponManager* weapon, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);
    void RenderEnemy(Enemy* enemy, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);
    void RenderAnimatedModel(AnimatedModel* model, const XMMATRIX& worldMatrix, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);

    void RenderMesh(Mesh* mesh, const XMMATRIX& worldMatrix, const XMMATRIX& viewMatrix, const XMMATRIX& projectionMatrix);

    void Cleanup();

private:
    // DirectX 11 объекты
    ComPtr<ID3D11Device>            m_pd3dDevice;
    ComPtr<ID3D11DeviceContext>     m_pImmediateContext;
    ComPtr<IDXGISwapChain>          m_pSwapChain;
    ComPtr<ID3D11RenderTargetView>  m_pRenderTargetView;
    ComPtr<ID3D11Texture2D>         m_pDepthStencil;
    ComPtr<ID3D11DepthStencilView>  m_pDepthStencilView;

    // Шейдеры и буферы
    ComPtr<ID3D11VertexShader>      m_pVertexShader;
    ComPtr<ID3D11PixelShader>       m_pPixelShader;
    ComPtr<ID3D11InputLayout>       m_pVertexLayout;
    ComPtr<ID3D11Buffer>            m_pConstantBuffer;
    ComPtr<ID3D11Buffer>            m_pModelConstantBuffer;
    ComPtr<ID3D11Buffer>            m_pBoneConstantBuffer;

    ComPtr<ID3D11SamplerState>      m_pSamplerLinear;

    // Параметры окна
    int m_width;
    int m_height;

    // Вспомогательные методы
    bool CreateDeviceAndSwapChain(HWND hWnd);
    bool CreateRenderViews();
    bool CreateShaders();
    bool CreateConstantBuffers();
};