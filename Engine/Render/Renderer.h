#pragma once
// ============================================================
//  Engine/Render/Renderer.h  —  DirectX 11
// ============================================================

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <string>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
    XMFLOAT3 tangent;
    int      boneIDs[4] = { -1, -1, -1, -1 };
    float    weights[4] = { 0,  0,  0,  0 };
};

struct ModelConstantBuffer { XMMATRIX mWorld; };

struct BoneConstantBuffer { XMMATRIX bones[100]; };

struct FrameConstantBuffer {
    XMMATRIX mView;
    XMMATRIX mProj;
    XMFLOAT3 lightDir;
    float    lightIntensity;
    XMFLOAT3 viewPos;
    float    _pad;
};

class Mesh {
public:
    virtual ~Mesh() = default;
    virtual ID3D11Buffer* const* GetVertexBufferAddress() const = 0;
    virtual ID3D11Buffer* GetIndexBuffer()         const = 0;
    virtual UINT                 GetIndexCount()          const = 0;
};

struct RenderObject {
    Mesh* mesh = nullptr;
    XMMATRIX  worldMatrix = XMMatrixIdentity();
    XMMATRIX* bones = nullptr;
    int       boneCount = 0;
    bool      castShadow = true;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(HWND hWnd, int width, int height);
    void Cleanup();
    void BeginFrame();
    void EndFrame();

    void RenderObject(const RenderObject& obj,
        const XMMATRIX& view,
        const XMMATRIX& proj);

    void SetFrameData(const XMMATRIX& view,
        const XMMATRIX& proj,
        const XMFLOAT3& viewPos,
        const XMFLOAT3& lightDir,
        float           lightIntensity);

    ID3D11Device* GetDevice()  const { return m_pDevice.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_pImmediateContext.Get(); }
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    ComPtr<ID3D11Device>            m_pDevice;
    ComPtr<ID3D11DeviceContext>     m_pImmediateContext;
    ComPtr<IDXGISwapChain>          m_pSwapChain;
    ComPtr<ID3D11RenderTargetView>  m_pRenderTargetView;
    ComPtr<ID3D11DepthStencilView>  m_pDepthStencilView;
    ComPtr<ID3D11Texture2D>         m_pDepthStencilBuffer;
    ComPtr<ID3D11VertexShader>      m_pVertexShader;
    ComPtr<ID3D11PixelShader>       m_pPixelShader;
    ComPtr<ID3D11InputLayout>       m_pInputLayout;
    ComPtr<ID3D11Buffer>            m_pModelConstantBuffer;
    ComPtr<ID3D11Buffer>            m_pBoneConstantBuffer;
    ComPtr<ID3D11Buffer>            m_pFrameConstantBuffer;
    int m_width;
    int m_height;

    bool CreateDeviceAndSwapChain(HWND hWnd);
    bool CreateRenderViews();
    bool CreateShaders();
    bool CreateConstantBuffers();
    void RenderMesh(Mesh* mesh, const XMMATRIX&, const XMMATRIX&, const XMMATRIX&);
};