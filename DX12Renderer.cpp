#pragma once

#include <d3d12.h>
#include "DX12Renderer.h"
#include "Debug.h"
#include "Constants.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <FBXSDK.h>
#include <stdexcept>

// Debug helper function to log the current function name and description
void LogDebugInfo(const std::string& functionName, const std::string& message) {
#ifdef _DEBUG
    std::string debugMessage = functionName + ": " + message;
    Debug::LogError(debugMessage.c_str());
#endif
}

// Function to create the DirectX 12 device
void DX12Renderer::CreateDevice() {
    try {
        UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
        // Enable debug layer for DirectX during development
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

        // Create DXGI factory to enumerate adapters (hardware/virtual graphics cards)
        HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            LogDebugInfo("CreateDevice", "Failed to create DXGI Factory.");
            throw std::runtime_error("CreateDXGIFactory2 failed");
        }

        // Enumerate the adapters (graphics cards)
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex) {
            // Log the adapter info (GPU information)
            LogAdapterInfo(adapter.Get());

            // Check if the adapter supports DirectX 12 (feature level)
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                // Skip the software adapters
                continue;
            }

            // Create the device using the selected adapter
            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
            if (SUCCEEDED(hr)) {
                break;
            }
        }

        if (!device) {
            LogDebugInfo("CreateDevice", "Failed to create DirectX 12 device.");
            throw std::runtime_error("DirectX 12 device creation failed");
        }

#ifdef _DEBUG
        CreateDebugLayer();
#endif
    }
    catch (const std::exception& e) {
        LogDebugInfo("CreateDevice", e.what());
        throw;
    }
}

// Create Command Queue for GPU command submission
void DX12Renderer::CreateCommandQueue() {
    try {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
        if (FAILED(hr)) {
            LogDebugInfo("CreateCommandQueue", "Failed to create command queue.");
            throw std::runtime_error("CreateCommandQueue failed");
        }
    }
    catch (const std::exception& e) {
        LogDebugInfo("CreateCommandQueue", e.what());
        throw;
    }
}

// Create swap chain for presenting rendered frames to the screen
HRESULT DX12Renderer::CreateSwapChain(HWND hwnd, HINSTANCE hInstance)
{
    HRESULT hr = S_OK;

    // Ensure that the factory is already created (DXGI Factory).
    if (!factory)
    {
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            Debug::LogError("Failed to create DXGI Factory.");
            return hr;
        }
    }

    // Describe the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = 0; // Use window width
    swapChainDesc.Height = 0; // Use window height
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Standard BGRA format
    swapChainDesc.Stereo = FALSE; // Stereo rendering disabled
    swapChainDesc.SampleDesc.Count = 1; // No multi-sampling
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // Render target output
    swapChainDesc.BufferCount = 2; // Double buffering for smooth presentation
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH; // Scaling mode
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Efficient swap effect
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // No alpha mode specified
    swapChainDesc.Flags = 0; // No flags

    // Create the swap chain using the command queue and hwnd (window handle)
    hr = factory->CreateSwapChainForHwnd(
        commandQueue.Get(),    // Command queue associated with the swap chain
        hwnd,                  // Window handle (HWND)
        &swapChainDesc,        // Swap chain description
        nullptr,               // No fullscreen transition
        nullptr,               // No restrict to monitor
        &swapChain1           // Output swap chain
    );

    if (FAILED(hr))
    {
        Debug::LogError("Failed to create swap chain.");
        return hr;
    }

    // Cast to the full swap chain interface
    hr = swapChain1.As(&swapChain);
    if (FAILED(hr))
    {
        Debug::LogError("Failed to cast swap chain interface.");
        return hr;
    }

    // Set up the swap chain size and format
    DXGI_SWAP_CHAIN_DESC swapChainDesc2 = {};
    swapChain->GetDesc(&swapChainDesc2);
    iWidth = swapChainDesc2.BufferDesc.Width;
    iHeight = swapChainDesc2.BufferDesc.Height;

    // Log success
    Debug::Log("Swap chain created successfully.");

    return hr;
}

// Create descriptor heaps for render target and depth stencil views
void DX12Renderer::CreateDescriptorHeaps() {
    try {
        // Render Target View (RTV) descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 2; // Double-buffered swap chain
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
        if (FAILED(hr)) {
            LogDebugInfo("CreateDescriptorHeaps", "Failed to create RTV descriptor heap.");
            throw std::runtime_error("CreateDescriptorHeap failed for RTV");
        }

        // Depth Stencil View (DSV) descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1; // Only one depth stencil view
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
        if (FAILED(hr)) {
            LogDebugInfo("CreateDescriptorHeaps", "Failed to create DSV descriptor heap.");
            throw std::runtime_error("CreateDescriptorHeap failed for DSV");
        }
    }
    catch (const std::exception& e) {
        LogDebugInfo("CreateDescriptorHeaps", e.what());
        throw;
    }
}

// Create render target views for swap chain buffers
void DX12Renderer::CreateRenderTargetViews() {
    try {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < 2; ++i) {
            HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
            if (FAILED(hr)) {
                LogDebugInfo("CreateRenderTargetViews", "Failed to get swap chain buffer.");
                throw std::runtime_error("GetSwapChainBuffer failed");
            }

            device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
    }
    catch (const std::exception& e) {
        LogDebugInfo("CreateRenderTargetViews", e.what());
        throw;
    }
}

// Create depth stencil buffer for depth testing and shadow mapping
void DX12Renderer::CreateDepthStencilBuffer() {
    try {
        D3D12_RESOURCE_DESC depthStencilDesc = {};
        depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthStencilDesc.Alignment = 0;
        depthStencilDesc.Width = Constants::DEFAULT_WINDOW_WIDTH;
        depthStencilDesc.Height = Constants::DEFAULT_WINDOW_HEIGHT;
        depthStencilDesc.DepthOrArraySize = 1;
        depthStencilDesc.MipLevels = 1;
        depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthStencilDesc.SampleDesc.Count = 1;
        depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        HRESULT hr = device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&depthStencilBuffer)
        );

        if (FAILED(hr)) {
            LogDebugInfo("CreateDepthStencilBuffer", "Failed to create depth stencil buffer.");
            throw std::runtime_error("CreateDepthStencilBuffer failed");
        }

        // Create depth stencil view
        device->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }
    catch (const std::exception& e) {
        LogDebugInfo("CreateDepthStencilBuffer", e.what());
        throw;
    }
}

// Create command list to execute render commands
void DX12Renderer::CreateCommandList() {
    try {
        HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
        if (FAILED(hr)) {
            LogDebugInfo("CreateCommandList", "Failed to create command allocator.");
            throw std::runtime_error("CreateCommandAllocator failed");
        }

        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
        if (FAILED(hr)) {
            LogDebugInfo("CreateCommandList", "Failed to create command list.");
            throw std::runtime_error("CreateCommandList failed");
        }
    }
    catch (const std::exception& e) {
        LogDebugInfo("CreateCommandList", e.what());
        throw;
    }
}

// Load shaders for the rendering pipeline
void DX12Renderer::LoadShaders() {
    try {
        // Loading and compiling shaders (vertex, pixel, etc.)
        // (You can replace this with actual shader loading logic, including file reading, compilation, etc.)
    }
    catch (const std::exception& e) {
        LogDebugInfo("LoadShaders", e.what());
        throw;
    }
}

// Enable the debug layer for DirectX 12
void DX12Renderer::CreateDebugLayer() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device.As(&infoQueue))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
    }
#endif
}

// Log adapter info for debugging purposes
void DX12Renderer::LogAdapterInfo(IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    LogDebugInfo("LogAdapterInfo", "Adapter description: " + std::wstring(desc.Description));
}

DX12Renderer::DX12Renderer() : frameIndex(0), fenceValue(0), fenceEvent(nullptr) {
    // Constructor logic
}

DX12Renderer::~DX12Renderer() {
    Cleanup();
}

void DX12Renderer::Initialize() {
    try {
        // Initialize DirectX 12 components
        CreateDevice();
        CreateCommandQueue();
        CreateSwapChain();
        CreateDescriptorHeaps();
        CreateRenderTargetViews();
        CreateDepthStencilBuffer();
        CreateCommandList();
        LoadShaders();

        // Debugging: Log adapter info
#ifdef _DEBUG
        CreateDebugLayer();
#endif

        Debug::LogInfo("DirectX 12 Renderer initialized.");
    }
    catch (const std::exception& e) {
        Debug::LogError("Error initializing DirectX 12: %s", e.what());
        throw;
    }
}

void DX12Renderer::RenderFrame() {
    try {
        // Record commands for this frame
        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), pipelineState.Get());

        // Clear render targets and depth stencil
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
        commandList->ClearRenderTargetView(rtvHandle, DirectX::Colors::CornflowerBlue, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Execute the rendering commands
        commandList->Close();
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(1, ppCommandLists);

        // Present the frame
        swapChain->Present(1, 0);

        // Sync with GPU
        fenceValue++;
        commandQueue->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }
    catch (const std::exception& e) {
        Debug::LogError("Error rendering DirectX 12 frame: %s", e.what());
        throw;
    }
}

void DX12Renderer::Cleanup() {
    if (fenceEvent) {
        CloseHandle(fenceEvent);
    }
}

#ifdef _DEBUG
void DX12Renderer::CreateDebugLayer() {
    // Enable debug layer for DirectX 12
}

void DX12Renderer::LogAdapterInfo(IDXGIAdapter1* adapter) {
    // Log adapter info for debugging
}
#endif
