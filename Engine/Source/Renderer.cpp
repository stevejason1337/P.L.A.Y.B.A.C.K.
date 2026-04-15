// Engine/Source/Renderer.cpp
#include "../Include/Renderer.h"

Renderer::Renderer(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = hWnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        NULL, 0, D3D11_SDK_VERSION, &sd, &swapChain, &device, NULL, &context);

    ComPtr<ID3D11Resource> backBuffer;
    swapChain->GetBuffer(0, __uuidof(ID3D11Resource), &backBuffer);
    device->CreateRenderTargetView(backBuffer.Get(), NULL, &targetView);
}

void Renderer::BeginFrame() {
    float color[] = { 0.1f, 0.1f, 0.15f, 1.0f }; // Твой фирменный тёмный фон
    context->ClearRenderTargetView(targetView.Get(), color);
    context->OMSetRenderTargets(1, targetView.GetAddressOf(), NULL);
}

void Renderer::EndFrame() {
    swapChain->Present(1, 0);
}