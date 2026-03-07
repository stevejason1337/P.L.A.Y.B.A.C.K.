#pragma once
// RenderAPI_DX11.h - DirectX 11 implementation of RenderAPI
// Windows only. Include AFTER RenderAPI.h
// To enable: #define RENDER_DX11_INCLUDED before including this file

#ifdef _WIN32
#define RENDER_DX11_INCLUDED

#include "RenderAPI.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cassert>

// Link libs — add to project or uncomment:
// #pragma comment(lib, "d3d11.lib")
// #pragma comment(lib, "d3dcompiler.lib")
// #pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

class DX11RenderAPI : public RenderAPI
{
public:
    Backend backend()     const override { return Backend::DX11; }
    const char* backendName() const override { return "DirectX 11"; }

    // ── Init ────────────────────────────────────────────
    bool init(void* windowHandle, int w, int h) override
    {
        _w = w; _h = h;
        HWND hwnd = (HWND)windowHandle;

        // Swap chain descriptor
        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 2;
        scd.BufferDesc.Width = w;
        scd.BufferDesc.Height = h;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hwnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            featureLevels, 1, D3D11_SDK_VERSION,
            &scd, &_swapChain, &_device, nullptr, &_context);

        if (FAILED(hr)) {
            printf("[DX11] Failed to create device: 0x%08X\n", hr);
            return false;
        }

        _createBackbuffer();
        _createDepthStencil(w, h);
        _setupRasterizerState();
        _setupDepthState();
        _setupBlendState();

        printf("[DX11] Device created. Feature level 11.0\n");
        return true;
    }

    void shutdown() override
    {
        _meshes.clear();
        _textures.clear();
        _shaders.clear();
        _rtv.Reset();
        _dsv.Reset();
        _depthTex.Reset();
        _swapChain.Reset();
        _context.Reset();
        _device.Reset();
    }

    void resize(int w, int h) override
    {
        _w = w; _h = h;
        _context->OMSetRenderTargets(0, nullptr, nullptr);
        _rtv.Reset();
        _swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        _createBackbuffer();
        _depthTex.Reset();
        _dsv.Reset();
        _createDepthStencil(w, h);
    }

    // ── Mesh ────────────────────────────────────────────
    MeshHandle createMesh(const MeshDesc& desc) override
    {
        DXMesh m;
        m.indexCount = desc.idxCount;
        m.skinned = desc.skinned;
        uint32_t stride = _strideForLayout(desc.layout) * sizeof(float);

        // Vertex buffer
        D3D11_BUFFER_DESC vbd = {};
        vbd.ByteWidth = desc.vertCount * stride;
        vbd.Usage = D3D11_USAGE_DEFAULT;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vd = { desc.vertices };
        _device->CreateBuffer(&vbd, &vd, &m.vb);

        // Index buffer
        D3D11_BUFFER_DESC ibd = {};
        ibd.ByteWidth = desc.idxCount * sizeof(uint32_t);
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA id2 = { desc.indices };
        _device->CreateBuffer(&ibd, &id2, &m.ib);

        m.stride = stride;
        uint32_t id = ++_nextId;
        _meshes[id] = std::move(m);
        return { id };
    }

    void destroyMesh(MeshHandle h) override { _meshes.erase(h.id); }

    // ── Texture ─────────────────────────────────────────
    TextureHandle createTexture(int w, int h, TextureFormat fmt,
        const void* data) override
    {
        DXTexture t;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;

        bool isDepth = (fmt == TextureFormat::DEPTH24 || fmt == TextureFormat::DEPTH32F);
        if (isDepth) {
            td.Format = DXGI_FORMAT_D32_FLOAT;
            td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        }
        else {
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (!data) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
        }

        D3D11_SUBRESOURCE_DATA sd = { data, (UINT)(w * 4) };
        _device->CreateTexture2D(&td, data ? &sd : nullptr, &t.tex);

        if (!isDepth) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format = td.Format;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvd.Texture2D.MipLevels = 1;
            _device->CreateShaderResourceView(t.tex.Get(), &srvd, &t.srv);
        }

        // Sampler
        D3D11_SAMPLER_DESC sampd = {};
        sampd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampd.AddressU = sampd.AddressV = sampd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampd.MaxAnisotropy = 1;
        sampd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampd.MaxLOD = D3D11_FLOAT32_MAX;
        _device->CreateSamplerState(&sampd, &t.sampler);

        uint32_t id = ++_nextId;
        _textures[id] = std::move(t);
        return { id };
    }

    TextureHandle loadTexture(const std::string& path) override
    {
        auto it = _texCache.find(path);
        if (it != _texCache.end()) return it->second;

        // Load via stb_image
        int w, h, ch;
        unsigned char* pixels = nullptr;
        // stbi_load needs to be available
        // #include "stb_image.h" in your precompiled header
        extern unsigned char* stbi_load(const char*, int*, int*, int*, int);
        pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            printf("[DX11] Texture not found: %s\n", path.c_str());
            return { 0 };
        }
        TextureHandle th = createTexture(w, h, TextureFormat::RGBA8, pixels);
        extern void stbi_image_free(void*);
        stbi_image_free(pixels);
        _texCache[path] = th;
        printf("[DX11] Loaded texture: %s (%dx%d)\n", path.c_str(), w, h);
        return th;
    }

    void destroyTexture(TextureHandle h) override { _textures.erase(h.id); }

    TextureHandle createRenderTarget(int w, int h, TextureFormat fmt) override
    {
        TextureHandle th = createTexture(w, h, fmt, nullptr);
        bool isDepth = (fmt != TextureFormat::RGBA8);

        auto& t = _textures[th.id];
        if (isDepth) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
            dsvd.Format = DXGI_FORMAT_D32_FLOAT;
            dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            _device->CreateDepthStencilView(t.tex.Get(), &dsvd, &t.dsv);
        }
        else {
            D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
            rtvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            _device->CreateRenderTargetView(t.tex.Get(), &rtvd, &t.rtv);
        }
        return th;
    }

    // ── Shader ──────────────────────────────────────────
    ShaderHandle createShader(const ShaderSource& src) override
    {
        if (!src.hlsl_vert || !src.hlsl_frag) {
            printf("[DX11] HLSL source missing for shader\n");
            return { 0 };
        }

        DXShader sh;

        // Compile vertex shader
        ComPtr<ID3DBlob> vsBlob, errBlob;
        HRESULT hr = D3DCompile(src.hlsl_vert, strlen(src.hlsl_vert),
            nullptr, nullptr, nullptr,
            "VSMain", "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &vsBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) printf("[DX11] VS error: %s\n", (char*)errBlob->GetBufferPointer());
            return { 0 };
        }
        _device->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, &sh.vs);

        // Compile pixel shader
        ComPtr<ID3DBlob> psBlob;
        hr = D3DCompile(src.hlsl_frag, strlen(src.hlsl_frag),
            nullptr, nullptr, nullptr,
            "PSMain", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &psBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) printf("[DX11] PS error: %s\n", (char*)errBlob->GetBufferPointer());
            return { 0 };
        }
        _device->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, &sh.ps);

        // Input layout — SKINNED layout (superset)
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT, 0,48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        _device->CreateInputLayout(layout, 5,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), &sh.inputLayout);

        // Constant buffer (256 bytes aligned — enough for 4 mat4s + misc)
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = 256; // min alignment
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        _device->CreateBuffer(&cbd, nullptr, &sh.cbuffer);

        // Bone constant buffer (100 * sizeof(mat4) = 6400 bytes)
        cbd.ByteWidth = 6400;
        _device->CreateBuffer(&cbd, nullptr, &sh.boneCBuffer);

        uint32_t id = ++_nextId;
        _shaders[id] = std::move(sh);
        return { id };
    }

    void destroyShader(ShaderHandle h) override { _shaders.erase(h.id); }

    // ── Frame ────────────────────────────────────────────
    void beginFrame() override
    {
        _context->OMSetRenderTargets(1, _rtv.GetAddressOf(), _dsv.Get());
        D3D11_VIEWPORT vp = { 0,0,(float)_w,(float)_h, 0.f, 1.f };
        _context->RSSetViewports(1, &vp);
    }

    void beginPass(const RenderPassDesc& pass) override
    {
        uint32_t w = pass.width ? pass.width : _w;
        uint32_t h = pass.height ? pass.height : _h;

        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;

        if (pass.renderTarget.valid()) {
            auto it = _textures.find(pass.renderTarget.id);
            if (it != _textures.end()) rtv = it->second.rtv.Get();
        }
        else {
            rtv = _rtv.Get();
        }
        if (pass.depthTarget.valid()) {
            auto it = _textures.find(pass.depthTarget.id);
            if (it != _textures.end()) dsv = it->second.dsv.Get();
        }
        else {
            dsv = _dsv.Get();
        }

        _context->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);

        D3D11_VIEWPORT vp = { 0,0,(float)w,(float)h, 0.f, 1.f };
        _context->RSSetViewports(1, &vp);

        if (pass.clearColorBuf && rtv)
            _context->ClearRenderTargetView(rtv, pass.clearColor);
        if (pass.clearDepth && dsv)
            _context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
    }

    void submit(const DrawCall& dc) override
    {
        auto shIt = _shaders.find(dc.shader.id);
        auto mhIt = _meshes.find(dc.mesh.id);
        if (shIt == _shaders.end() || mhIt == _meshes.end()) return;

        auto& sh = shIt->second;
        auto& m = mhIt->second;

        // Input layout + shaders
        _context->IASetInputLayout(sh.inputLayout.Get());
        _context->VSSetShader(sh.vs.Get(), nullptr, 0);
        _context->PSSetShader(sh.ps.Get(), nullptr, 0);
        _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Vertex/index buffers
        UINT offset = 0;
        _context->IASetVertexBuffers(0, 1, m.vb.GetAddressOf(), &m.stride, &offset);
        _context->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);

        // Upload uniforms to cbuffer
        _uploadUniforms(sh, dc.uniforms);

        // Bone matrices
        if (dc.boneMatrices && dc.boneCount > 0)
            _uploadBones(sh, dc.boneMatrices, dc.boneCount);

        // Rasterizer / depth state
        _context->RSSetState(dc.cullBackface ? _rsStateCull.Get() : _rsStateNoCull.Get());
        _context->OMSetDepthStencilState(dc.depthTest ? _dssEnabled.Get() : _dssDisabled.Get(), 0);
        _context->OMSetBlendState(dc.blendAlpha ? _bsAlpha.Get() : _bsOpaque.Get(), nullptr, 0xFFFFFFFF);

        _context->DrawIndexed(m.indexCount, 0, 0);
    }

    void endPass()  override {}
    void endFrame() override { _swapChain->Present(1, 0); }  // VSync on

    void setViewport(int x, int y, int w, int h) override
    {
        D3D11_VIEWPORT vp = { (float)x,(float)y,(float)w,(float)h, 0.f,1.f };
        _context->RSSetViewports(1, &vp);
    }

    void readPixels(int x, int y, int w, int h, void* out) override
    {
        // Staging texture readback — omitted for brevity
        (void)x; (void)y; (void)w; (void)h; (void)out;
    }

    void bindShader(ShaderHandle sh) override { _boundShader = sh; }

    void setUniform(ShaderHandle sh, const UniformData& u) override
    {
        // Store for next submit
        (void)sh; (void)u;
    }

    void drawMesh(MeshHandle mesh) override
    {
        auto it = _meshes.find(mesh.id);
        if (it == _meshes.end()) return;
        UINT stride = it->second.stride, offset = 0;
        _context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
        _context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        _context->DrawIndexed(it->second.indexCount, 0, 0);
    }

    // DX11-specific accessors
    ID3D11Device* getDevice() { return _device.Get(); }
    ID3D11DeviceContext* getContext() { return _context.Get(); }

private:
    // ── DX11 resources ────────────────────────────────────
    ComPtr<ID3D11Device>           _device;
    ComPtr<ID3D11DeviceContext>    _context;
    ComPtr<IDXGISwapChain>         _swapChain;
    ComPtr<ID3D11RenderTargetView> _rtv;
    ComPtr<ID3D11DepthStencilView> _dsv;
    ComPtr<ID3D11Texture2D>        _depthTex;

    // States
    ComPtr<ID3D11RasterizerState>    _rsStateCull, _rsStateNoCull;
    ComPtr<ID3D11DepthStencilState>  _dssEnabled, _dssDisabled;
    ComPtr<ID3D11BlendState>         _bsOpaque, _bsAlpha;

    struct DXMesh {
        ComPtr<ID3D11Buffer> vb, ib;
        uint32_t indexCount = 0;
        uint32_t stride = 0;
        bool     skinned = false;
    };
    struct DXTexture {
        ComPtr<ID3D11Texture2D>           tex;
        ComPtr<ID3D11ShaderResourceView>  srv;
        ComPtr<ID3D11SamplerState>        sampler;
        ComPtr<ID3D11RenderTargetView>    rtv;
        ComPtr<ID3D11DepthStencilView>    dsv;
    };
    struct DXShader {
        ComPtr<ID3D11VertexShader>  vs;
        ComPtr<ID3D11PixelShader>   ps;
        ComPtr<ID3D11InputLayout>   inputLayout;
        ComPtr<ID3D11Buffer>        cbuffer;
        ComPtr<ID3D11Buffer>        boneCBuffer;
    };

    std::unordered_map<uint32_t, DXMesh>    _meshes;
    std::unordered_map<uint32_t, DXTexture> _textures;
    std::unordered_map<uint32_t, DXShader>  _shaders;
    std::unordered_map<std::string, TextureHandle> _texCache;
    uint32_t   _nextId = 0;
    ShaderHandle _boundShader;

    // ── Setup helpers ────────────────────────────────────
    void _createBackbuffer()
    {
        ComPtr<ID3D11Texture2D> bb;
        _swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
        _device->CreateRenderTargetView(bb.Get(), nullptr, &_rtv);
    }

    void _createDepthStencil(int w, int h)
    {
        D3D11_TEXTURE2D_DESC dd = {};
        dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dd.SampleDesc.Count = 1;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        _device->CreateTexture2D(&dd, nullptr, &_depthTex);
        _device->CreateDepthStencilView(_depthTex.Get(), nullptr, &_dsv);
    }

    void _setupRasterizerState()
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable = TRUE;
        _device->CreateRasterizerState(&rd, &_rsStateCull);
        rd.CullMode = D3D11_CULL_NONE;
        _device->CreateRasterizerState(&rd, &_rsStateNoCull);
    }

    void _setupDepthState()
    {
        D3D11_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS;
        _device->CreateDepthStencilState(&ds, &_dssEnabled);
        ds.DepthEnable = FALSE;
        _device->CreateDepthStencilState(&ds, &_dssDisabled);
    }

    void _setupBlendState()
    {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = FALSE;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        _device->CreateBlendState(&bd, &_bsOpaque);
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        _device->CreateBlendState(&bd, &_bsAlpha);
    }

    static uint32_t _strideForLayout(VertexLayout l)
    {
        switch (l) {
        case VertexLayout::POS3:           return 3;
        case VertexLayout::POS3_UV2:       return 5;
        case VertexLayout::POS3_NORM3_UV2: return 8;
        case VertexLayout::SKINNED:        return 16;
        }
        return 8;
    }

    // Pack uniforms into cbuffer and upload
    void _uploadUniforms(DXShader& sh, const std::vector<UniformData>& uniforms)
    {
        // Simple layout: just pack floats sequentially
        // In a real engine you'd use reflection or a predefined cbuffer struct
        alignas(16) float cbData[64] = {}; // 256 bytes
        int offset = 0;
        int texSlot = 0;

        for (auto& u : uniforms) {
            switch (u.type) {
            case UniformData::FLOAT: cbData[offset++] = u.f; break;
            case UniformData::INT:   cbData[offset++] = (float)u.i; break;
            case UniformData::VEC3:  memcpy(&cbData[offset], u.v3, 12); offset += 3; break;
            case UniformData::VEC4:  memcpy(&cbData[offset], u.v4, 16); offset += 4; break;
            case UniformData::MAT4:  memcpy(&cbData[offset], u.m4, 64); offset += 16; break;
            case UniformData::SAMPLER: {
                // Bind texture to pixel shader
                auto it = _textures.find(u.texture.id);
                if (it != _textures.end()) {
                    _context->PSSetShaderResources(u.slot, 1, it->second.srv.GetAddressOf());
                    _context->PSSetSamplers(u.slot, 1, it->second.sampler.GetAddressOf());
                }
                break;
            }
            default: break;
            }
            if (offset >= 60) break;
        }

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(_context->Map(sh.cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, cbData, sizeof(cbData));
            _context->Unmap(sh.cbuffer.Get(), 0);
        }
        _context->VSSetConstantBuffers(0, 1, sh.cbuffer.GetAddressOf());
        _context->PSSetConstantBuffers(0, 1, sh.cbuffer.GetAddressOf());
    }

    void _uploadBones(DXShader& sh, const float* bones, int count)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(_context->Map(sh.boneCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, bones, count * 64);
            _context->Unmap(sh.boneCBuffer.Get(), 0);
        }
        _context->VSSetConstantBuffers(1, 1, sh.boneCBuffer.GetAddressOf());
    }
};

#endif // _WIN32