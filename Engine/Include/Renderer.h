// Engine/Include/Renderer.h
#pragma once
#include <d3d11.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class Renderer {
public:
    Renderer(HWND hWnd);
    void BeginFrame();
    void EndFrame();

    ID3D11Device* GetDevice() { return device.Get(); }
    ID3D11DeviceContext* GetContext() { return context.Get(); }

private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> targetView;
};