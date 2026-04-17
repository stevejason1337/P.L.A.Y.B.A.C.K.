#pragma once
// RenderAPI_DX11.h - DirectX 11 implementation of RenderAPI
// Windows only. Include AFTER RenderAPI.h + RenderAPI_GL.h
// Defining RENDER_DX11_INCLUDED enables DX11 in RenderAPI::create()

#ifdef _WIN32
#define RENDER_DX11_INCLUDED

#include "Renderapi.h"
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
#include <unordered_map>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxgi.lib")
using Microsoft::WRL::ComPtr;

// ── stb_image forward decls (header included in ModelLoader/main) ──
#ifndef STBI_INCLUDE_STB_IMAGE_H
extern "C" {
    extern unsigned char* stbi_load(const char*, int*, int*, int*, int);
    extern void           stbi_image_free(void*);
}
#endif

class DX11RenderAPI : public RenderAPI
{
public:
    Backend     backend()     const override { return Backend::DX11; }
    const char* backendName() const override { return "DirectX 11"; }

    // ── Init ────────────────────────────────────────────
    bool init(void* windowHandle, int w, int h) override
    {
        _w = w; _h = h;
        HWND hwnd = (HWND)windowHandle;
        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 2; scd.BufferDesc.Width = w; scd.BufferDesc.Height = h;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate = { 60,1 };
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE; scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0 };
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            fl, 1, D3D11_SDK_VERSION, &scd, &_sc, &_dev, nullptr, &_ctx);
        if (FAILED(hr)) { printf("[DX11] Device create failed: 0x%08X\n", hr); return false; }
        _createBackbuffer();
        _createDepth(w, h);
        _setupStates();
        // Create scene cbuffer (slot b0) — must be >= sizeof(SceneUniforms) aligned to 16
        _createCB(_sceneCB, sizeof(SceneUniforms));
        // Create bone cbuffer (slot b1) — 100 mat4
        _createCB(_boneCB, 100 * 64);
        printf("[DX11] Init OK (%dx%d)\n", w, h);
        return true;
    }

    void shutdown() override {
        _meshes.clear(); _textures.clear(); _shaders.clear();
        _sceneCB.Reset(); _boneCB.Reset();
        _rtv.Reset(); _dsv.Reset(); _depthTex.Reset();
        _sc.Reset(); _ctx.Reset(); _dev.Reset();
    }

    void resize(int w, int h) override {
        _w = w; _h = h;
        _ctx->OMSetRenderTargets(0, nullptr, nullptr);
        _rtv.Reset();
        _sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        _createBackbuffer();
        _depthTex.Reset(); _dsv.Reset();
        _createDepth(w, h);
    }

    // ── Mesh ────────────────────────────────────────────
    MeshHandle createMesh(const MeshDesc& desc) override {
        DXMesh m;
        m.indexCount = desc.idxCount;
        m.stride = _strideForLayout(desc.layout) * sizeof(float);
        D3D11_BUFFER_DESC vbd = { desc.vertCount * m.stride,D3D11_USAGE_DEFAULT,D3D11_BIND_VERTEX_BUFFER,0,0,0 };
        D3D11_SUBRESOURCE_DATA vd = { desc.vertices };
        _dev->CreateBuffer(&vbd, &vd, &m.vb);
        D3D11_BUFFER_DESC ibd = { desc.idxCount * 4,D3D11_USAGE_DEFAULT,D3D11_BIND_INDEX_BUFFER,0,0,0 };
        D3D11_SUBRESOURCE_DATA id2 = { desc.indices };
        _dev->CreateBuffer(&ibd, &id2, &m.ib);
        uint32_t id = ++_nextId; _meshes[id] = std::move(m); return { id };
    }
    void destroyMesh(MeshHandle h) override { _meshes.erase(h.id); }

    // ── Texture ─────────────────────────────────────────
    TextureHandle createTexture(int w, int h, TextureFormat fmt, const void* data) override {
        DXTex t;
        bool isDepth = (fmt != TextureFormat::RGBA8);
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.ArraySize = 1; td.SampleDesc.Count = 1;
        if (data && !isDepth) {
            td.MipLevels = 0; // auto mips
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            td.Usage = D3D11_USAGE_DEFAULT;
            _dev->CreateTexture2D(&td, nullptr, &t.tex);
            // Upload mip0
            _ctx->UpdateSubresource(t.tex.Get(), 0, nullptr, data, w * 4, 0);
            // SRV for all mip levels
            D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
            svd.Format = td.Format; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            svd.Texture2D.MostDetailedMip = 0; svd.Texture2D.MipLevels = (UINT)-1;
            _dev->CreateShaderResourceView(t.tex.Get(), &svd, &t.srv);
            _ctx->GenerateMips(t.srv.Get());
        }
        else if (!isDepth) {
            // render target (no data)
            td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            td.Usage = D3D11_USAGE_DEFAULT;
            _dev->CreateTexture2D(&td, nullptr, &t.tex);
            D3D11_SHADER_RESOURCE_VIEW_DESC svd = { td.Format,D3D11_SRV_DIMENSION_TEXTURE2D,{} };
            svd.Texture2D.MipLevels = 1;
            _dev->CreateShaderResourceView(t.tex.Get(), &svd, &t.srv);
        }
        else {
            // depth texture
            td.MipLevels = 1;
            td.Format = DXGI_FORMAT_R32_TYPELESS;
            td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            _dev->CreateTexture2D(&td, nullptr, &t.tex);
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = { DXGI_FORMAT_D32_FLOAT,D3D11_DSV_DIMENSION_TEXTURE2D,{} };
            _dev->CreateDepthStencilView(t.tex.Get(), &dsvd, &t.dsv);
        }
        // Linear + aniso sampler for color textures
        if (!isDepth) {
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter = D3D11_FILTER_ANISOTROPIC;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.MaxAnisotropy = 4; sd.MaxLOD = D3D11_FLOAT32_MAX;
            sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
            _dev->CreateSamplerState(&sd, &t.sampler);
        }
        uint32_t id = ++_nextId; _textures[id] = std::move(t); return { id };
    }

    TextureHandle loadTexture(const std::string& path) override {
        auto it = _texCache.find(path); if (it != _texCache.end()) return it->second;
        int w, h, ch;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!px) { printf("[DX11] Texture not found: %s\n", path.c_str()); return { 0 }; }
        TextureHandle th = createTexture(w, h, TextureFormat::RGBA8, px);
        stbi_image_free(px);
        _texCache[path] = th;
        printf("[DX11] Texture loaded: %s (%dx%d)\n", path.c_str(), w, h);
        return th;
    }
    void destroyTexture(TextureHandle h) override { _textures.erase(h.id); }

    TextureHandle createRenderTarget(int w, int h, TextureFormat fmt) override {
        TextureHandle th = createTexture(w, h, fmt, nullptr);
        bool isDepth = (fmt != TextureFormat::RGBA8);
        auto& t = _textures[th.id];
        if (!isDepth) {
            D3D11_RENDER_TARGET_VIEW_DESC rd = { DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_RTV_DIMENSION_TEXTURE2D,{} };
            _dev->CreateRenderTargetView(t.tex.Get(), &rd, &t.rtv);
        }
        return th;
    }

    // ── Shader ──────────────────────────────────────────
    ShaderHandle createShader(const ShaderSource& src) override {
        if (!src.hlsl_combined) { printf("[DX11] HLSL missing\n"); return { 0 }; }
        DXShader sh;
        ComPtr<ID3DBlob> vsBlob, psBlob, err;
        HRESULT hr = D3DCompile(src.hlsl_combined, strlen(src.hlsl_combined), nullptr, nullptr, nullptr,
            "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &err);
        if (FAILED(hr)) { if (err) printf("[DX11] VS: %s\n", (char*)err->GetBufferPointer()); return { 0 }; }
        _dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &sh.vs);

        hr = D3DCompile(src.hlsl_combined, strlen(src.hlsl_combined), nullptr, nullptr, nullptr,
            "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &err);
        if (FAILED(hr)) { if (err) printf("[DX11] PS: %s\n", (char*)err->GetBufferPointer()); return { 0 }; }
        _dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &sh.ps);

        // Input layout — SKINNED superset covers all layouts
        D3D11_INPUT_ELEMENT_DESC il[] = {
            {"POSITION",    0,DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",      0,DXGI_FORMAT_R32G32B32_FLOAT,   0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",    0,DXGI_FORMAT_R32G32_FLOAT,      0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BLENDWEIGHT", 0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        _dev->CreateInputLayout(il, 5, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &sh.il);
        uint32_t id = ++_nextId; _shaders[id] = std::move(sh); return { id };
    }
    void destroyShader(ShaderHandle h) override { _shaders.erase(h.id); }

    // ── Frame ────────────────────────────────────────────
    void beginFrame() override {
        _ctx->OMSetRenderTargets(1, _rtv.GetAddressOf(), _dsv.Get());
        D3D11_VIEWPORT vp = { 0,0,(float)_w,(float)_h,0,1 };
        _ctx->RSSetViewports(1, &vp);
    }

    void beginPass(const RenderPassDesc& pass) override {
        ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;
        if (pass.renderTarget.valid()) { auto it = _textures.find(pass.renderTarget.id); if (it != _textures.end()) rtv = it->second.rtv.Get(); }
        else rtv = _rtv.Get();
        if (pass.depthTarget.valid()) { auto it = _textures.find(pass.depthTarget.id); if (it != _textures.end()) dsv = it->second.dsv.Get(); }
        else dsv = _dsv.Get();
        _ctx->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);
        uint32_t w = pass.width ? pass.width : _w, h = pass.height ? pass.height : _h;
        D3D11_VIEWPORT vp = { (float)0,(float)0,(float)w,(float)h,0,1 }; _ctx->RSSetViewports(1, &vp);
        if (pass.clearColorBuf && rtv) _ctx->ClearRenderTargetView(rtv, pass.clearColor);
        if (pass.clearDepth && dsv)    _ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1, 0);
    }

    void submit(const DrawCall& dc) override {
        auto shIt = _shaders.find(dc.shader.id); auto mhIt = _meshes.find(dc.mesh.id);
        if (shIt == _shaders.end() || mhIt == _meshes.end()) return;
        auto& sh = shIt->second; auto& m = mhIt->second;

        _ctx->IASetInputLayout(sh.il.Get());
        _ctx->VSSetShader(sh.vs.Get(), nullptr, 0);
        _ctx->PSSetShader(sh.ps.Get(), nullptr, 0);
        _ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT off = 0; _ctx->IASetVertexBuffers(0, 1, m.vb.GetAddressOf(), &m.stride, &off);
        _ctx->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);

        // ── Upload SceneUniforms to cbuffer b0 ───────────
        if (dc.sceneUniforms) {
            _upload(_sceneCB, dc.sceneUniforms, sizeof(SceneUniforms));
            _ctx->VSSetConstantBuffers(0, 1, _sceneCB.GetAddressOf());
            _ctx->PSSetConstantBuffers(0, 1, _sceneCB.GetAddressOf());
        }

        // ── Diffuse texture slot 0 ────────────────────────
        if (dc.diffuseTexture.valid()) {
            auto tIt = _textures.find(dc.diffuseTexture.id);
            if (tIt != _textures.end() && tIt->second.srv) {
                _ctx->PSSetShaderResources(0, 1, tIt->second.srv.GetAddressOf());
                _ctx->PSSetSamplers(0, 1, tIt->second.sampler.GetAddressOf());
            }
        }
        else {
            ID3D11ShaderResourceView* nullSRV = nullptr;
            _ctx->PSSetShaderResources(0, 1, &nullSRV);
        }

        // ── Legacy sampler uniforms ───────────────────────
        for (auto& u : dc.uniforms) {
            if (u.type == UniformData::SAMPLER) {
                auto tIt = _textures.find(u.texture.id);
                if (tIt != _textures.end() && tIt->second.srv)
                    _ctx->PSSetShaderResources(u.slot, 1, tIt->second.srv.GetAddressOf());
            }
        }

        // ── Bone matrices to cbuffer b1 ───────────────────
        if (dc.boneMatrices && dc.boneCount > 0) {
            _upload(_boneCB, dc.boneMatrices, dc.boneCount * 64);
            _ctx->VSSetConstantBuffers(1, 1, _boneCB.GetAddressOf());
        }

        // ── Render states ─────────────────────────────────
        _ctx->RSSetState(dc.cullBackface ? _rsCull.Get() : _rsNoCull.Get());
        _ctx->OMSetDepthStencilState(dc.depthTest ? _dssOn.Get() : _dssOff.Get(), 0);
        _ctx->OMSetBlendState(dc.blendAlpha ? _bsAlpha.Get() : _bsOpaque.Get(), nullptr, 0xFFFFFFFF);

        _ctx->DrawIndexed(m.indexCount, 0, 0);
    }

    void endPass()  override {}
    void endFrame() override { _sc->Present(1, 0); }
    void setViewport(int x, int y, int w, int h) override { D3D11_VIEWPORT vp = { (float)x,(float)y,(float)w,(float)h,0,1 }; _ctx->RSSetViewports(1, &vp); }
    void readPixels(int, int, int, int, void*) override {}
    void bindShader(ShaderHandle sh) override { _boundSh = sh; }
    void setUniform(ShaderHandle, const UniformData&) override {}
    void drawMesh(MeshHandle mesh) override {
        auto it = _meshes.find(mesh.id); if (it == _meshes.end()) return;
        UINT off = 0; _ctx->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &it->second.stride, &off);
        _ctx->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        _ctx->DrawIndexed(it->second.indexCount, 0, 0);
    }

    ID3D11Device* getDevice() { return _dev.Get(); }
    ID3D11DeviceContext* getContext() { return _ctx.Get(); }

private:
    ComPtr<ID3D11Device>           _dev;
    ComPtr<ID3D11DeviceContext>    _ctx;
    ComPtr<IDXGISwapChain>         _sc;
    ComPtr<ID3D11RenderTargetView> _rtv;
    ComPtr<ID3D11DepthStencilView> _dsv;
    ComPtr<ID3D11Texture2D>        _depthTex;
    // Shared constant buffers
    ComPtr<ID3D11Buffer>           _sceneCB;   // b0: SceneUniforms
    ComPtr<ID3D11Buffer>           _boneCB;    // b1: bones[100]
    // Render states
    ComPtr<ID3D11RasterizerState>   _rsCull, _rsNoCull;
    ComPtr<ID3D11DepthStencilState> _dssOn, _dssOff;
    ComPtr<ID3D11BlendState>        _bsOpaque, _bsAlpha;

    struct DXMesh { ComPtr<ID3D11Buffer> vb, ib; uint32_t indexCount = 0, stride = 0; };
    struct DXTex {
        ComPtr<ID3D11Texture2D>          tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11SamplerState>       sampler;
        ComPtr<ID3D11RenderTargetView>   rtv;
        ComPtr<ID3D11DepthStencilView>   dsv;
    };
    struct DXShader {
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader>  ps;
        ComPtr<ID3D11InputLayout>  il;
    };

    std::unordered_map<uint32_t, DXMesh>    _meshes;
    std::unordered_map<uint32_t, DXTex>     _textures;
    std::unordered_map<uint32_t, DXShader>  _shaders;
    std::unordered_map<std::string, TextureHandle> _texCache;
    uint32_t   _nextId = 0;
    ShaderHandle _boundSh;

    void _createBackbuffer() {
        ComPtr<ID3D11Texture2D> bb; _sc->GetBuffer(0, IID_PPV_ARGS(&bb));
        _dev->CreateRenderTargetView(bb.Get(), nullptr, &_rtv);
    }
    void _createDepth(int w, int h) {
        D3D11_TEXTURE2D_DESC dd = {};
        dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        _dev->CreateTexture2D(&dd, nullptr, &_depthTex);
        _dev->CreateDepthStencilView(_depthTex.Get(), nullptr, &_dsv);
    }
    void _setupStates() {
        D3D11_RASTERIZER_DESC rd = { D3D11_FILL_SOLID,D3D11_CULL_BACK,TRUE,0,0,0,TRUE };
        _dev->CreateRasterizerState(&rd, &_rsCull);
        rd.CullMode = D3D11_CULL_NONE; _dev->CreateRasterizerState(&rd, &_rsNoCull);
        D3D11_DEPTH_STENCIL_DESC ds = { TRUE,D3D11_DEPTH_WRITE_MASK_ALL,D3D11_COMPARISON_LESS };
        _dev->CreateDepthStencilState(&ds, &_dssOn);
        ds.DepthEnable = FALSE; _dev->CreateDepthStencilState(&ds, &_dssOff);
        D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        _dev->CreateBlendState(&bd, &_bsOpaque);
        bd.RenderTarget[0] = { TRUE,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_OP_ADD,D3D11_BLEND_ONE,D3D11_BLEND_ZERO,D3D11_BLEND_OP_ADD,D3D11_COLOR_WRITE_ENABLE_ALL };
        _dev->CreateBlendState(&bd, &_bsAlpha);
    }
    void _createCB(ComPtr<ID3D11Buffer>& cb, size_t byteWidth) {
        UINT sz = ((UINT)byteWidth + 15u) & ~15u;
        D3D11_BUFFER_DESC bd = { sz, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
        _dev->CreateBuffer(&bd, nullptr, &cb);
    }
    void _upload(ComPtr<ID3D11Buffer>& cb, const void* data, size_t sz) {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(_ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
        {
            memcpy(ms.pData, data, sz); _ctx->Unmap(cb.Get(), 0);
        }
    }
    static uint32_t _strideForLayout(VertexLayout l) {
        switch (l) {
        case VertexLayout::POS3:           return 3;
        case VertexLayout::POS3_UV2:       return 5;
        case VertexLayout::POS3_NORM3_UV2: return 8;
        case VertexLayout::SKINNED:        return 16;
        } return 8;
    }
};

#endif // _WIN32