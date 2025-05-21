#pragma once

#if defined(__USE_DIRECTX_12__)

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <d3dcompiler.h>

#include "Debug.h"
#include "Renderer.h"  // Ensure Renderer base class is included
#include "Vector2.h"   // Ensure Vector2 is defined

class DX12Renderer : public Renderer {
public:
    DX12Renderer();
    virtual ~DX12Renderer();

//    void Initialize() override;
    void RenderFrame() override;
    void Cleanup() override;

//    void DrawRectangle(const Vector2& position, const Vector2& size, const Color& color, bool is2D);
//    void DrawText(const std::string& text, const Vector2& position, const Color& color);
//    void DrawTexture(const std::string& textureId, const Vector2& position, const Vector2& size, const Color& tintColor, bool is2D);

private:
    void CreateDevice();
    void CreateCommandQueue();
    HRESULT CreateSwapChain(HWND hwnd, HINSTANCE hInstance);
    void CreateDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();
    void CreateCommandList();
    void LoadShaders();

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTargets[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    UINT iWidth;
    UINT iHeight;
    UINT frameIndex;
    UINT64 fenceValue;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent;

    // Debugging
    void LogAdapterInfo(IDXGIAdapter1* adapter);
    void CreateDebugLayer();
};

#endif //#if defined(__USE_DIRECTX_12__)