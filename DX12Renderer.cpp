#define NOMINMAX

#include "Includes.h"

// DirectX 12 Required Headers & Linking
#include "Renderer.h"

// Perform Renderer to USE Test.
// This is done to ensure we only include required code.
// Meaning, if we are NOT using this Renderer, forget it
// and DO NOT include its code.
#if defined(__USE_DIRECTX_12__)
#include "DX12Renderer.h"
#include "Debug.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "DX_FXManager.h"
#include "GUIManager.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "MoviePlayer.h"

#if defined(__USE_MP3PLAYER__)
#include "WinMediaPlayer.h"
#elif defined(__USE_XMPLAYER__)
#include "XMMODPlayer.h"
#endif

#include <d3dcompiler.h>
#include "d3dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Static member initialization
std::mutex DX12Renderer::s_renderMutex;
std::mutex DX12Renderer::s_loaderMutex;

class LightsManager;

extern HWND hwnd;
extern HINSTANCE hInst;
extern GUIManager guiManager;
extern Debug debug;
extern SystemUtils sysUtils;
extern SceneManager scene;
extern ThreadManager threadManager;
extern FXManager fxManager;
extern Vector2 myMouseCoords;
extern Model models[MAX_MODELS];
extern LightsManager lightsManager;
extern MoviePlayer moviePlayer;
extern WindowMetrics winMetrics;

extern bool bResizing;
extern std::atomic<bool> bResizeInProgress;                    // Prevents multiple resize operations
extern std::atomic<bool> bFullScreenTransition;                // Prevents handling during fullscreen transitions

#if defined(__USE_MP3PLAYER__)
extern MediaPlayer player;
#elif defined(__USE_XMPLAYER__)
extern XMMODPlayer xmPlayer;
#endif

// Constructor/Destructor
DX12Renderer::DX12Renderer() : m_frameIndex(0), m_fenceValue(0), m_fenceEvent(nullptr)
{
    // IMPORTANT: Set the RendererType to DirectX 12 SO that the Engine knows which renderer to use and refer too.
    sName = threadManager.getThreadName(THREAD_RENDERER);
    RenderType = RendererType::RT_DirectX12;

    // Initialize all frame contexts to zero
    SecureZeroMemory(&m_frameContexts, sizeof(m_frameContexts));

    // Initialize descriptor heaps to zero
    SecureZeroMemory(&m_rtvHeap, sizeof(m_rtvHeap));
    SecureZeroMemory(&m_dsvHeap, sizeof(m_dsvHeap));
    SecureZeroMemory(&m_cbvSrvUavHeap, sizeof(m_cbvSrvUavHeap));
    SecureZeroMemory(&m_samplerHeap, sizeof(m_samplerHeap));

    // Initialize texture arrays to zero
    SecureZeroMemory(&m_d3d12Textures, sizeof(m_d3d12Textures));
    SecureZeroMemory(&m_d2dTextures, sizeof(m_d2dTextures));
    SecureZeroMemory(&My2DBlitQueue, sizeof(My2DBlitQueue));
    SecureZeroMemory(&screenModes, sizeof(screenModes));

    // Initialize DirectX 11-12 compatibility context
    SecureZeroMemory(&m_dx11Dx12Compat, sizeof(m_dx11Dx12Compat));
    m_dx11Dx12Compat.bDX11Available = false;
    m_dx11Dx12Compat.bDX12Available = false;
    m_dx11Dx12Compat.bUsingDX11Fallback = false;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Constructor initialized successfully.");
#endif
}

DX12Renderer::~DX12Renderer()
{
    if (bIsDestroyed.load()) return;

    Cleanup();
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Cleaned up and Destroyed!");
    bIsDestroyed.store(true);
}

//-----------------------------------------
// Core DirectX 12 Device Creation
//-----------------------------------------
void DX12Renderer::CreateDevice() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating DirectX 12 device...");
#endif

    try {
        UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
        // Enable the debug layer for DirectX 12 during development
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            // Enable the debug layer
            debugController->EnableDebugLayer();

            // Enable additional debug features for better error reporting
            ComPtr<ID3D12Debug1> debugController1;
            if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1))))
            {
                debugController1->SetEnableGPUBasedValidation(TRUE);
            }

            // Enable GPU-based validation and shader debugging
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Debug layer enabled successfully.");
#endif
        }
        else
        {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to enable debug layer.");
#endif
        }
#endif

        // Create DXGI factory to enumerate adapters (hardware/virtual graphics cards)
        ComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create DXGI Factory.");
            ThrowError("CreateDXGIFactory2 failed");
            return;
        }

        // Select the best available adapter
        ComPtr<IDXGIAdapter4> bestAdapter = SelectBestAdapter();
        if (!bestAdapter) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: No suitable DirectX 12 adapter found.");
            ThrowError("No DirectX 12 compatible adapter found");
            return;
        }

        // Log the selected adapter information
        LogAdapterInfo(bestAdapter.Get());

        // Create the DirectX 12 device using the selected adapter
        hr = D3D12CreateDevice(bestAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3d12Device));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create DirectX 12 device.");
            ThrowError("DirectX 12 device creation failed");
            return;
        }

        // Set the device name for debugging purposes
        m_d3d12Device->SetName(L"DX12Renderer_MainDevice");

        // Mark DirectX 12 as available in compatibility context
        m_dx11Dx12Compat.bDX12Available = true;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: DirectX 12 device created successfully.");
#endif

        // Create additional debug layer for device-specific debugging
        CreateDebugLayer();
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateDevice: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create DirectX 12 Command Queue
//-----------------------------------------
void DX12Renderer::CreateCommandQueue() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating command queue...");
#endif

    try {
        // Describe the command queue for direct command list execution
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;                        // No special flags
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;                        // Direct command list type for graphics
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;               // Normal priority
        queueDesc.NodeMask = 0;                                                 // Single GPU node

        // Create the command queue
        HRESULT hr = m_d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create command queue.");
            ThrowError("CreateCommandQueue failed");
            return;
        }

        // Set the command queue name for debugging purposes
        m_commandQueue->SetName(L"DX12Renderer_MainCommandQueue");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Command queue created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateCommandQueue: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create DirectX 12 Swap Chain
//-----------------------------------------
void DX12Renderer::CreateSwapChain(HWND hwnd)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating swap chain...");
#endif

    try {
        // Create DXGI factory if not already created
        ComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create DXGI Factory for swap chain.");
            ThrowError("Failed to create DXGI Factory for swap chain");
            return;
        }

        // Describe the swap chain for optimal DirectX 12 performance
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = 0;                                                // Use window width automatically
        swapChainDesc.Height = 0;                                               // Use window height automatically
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;                     // Standard RGBA format for DirectX 12
        swapChainDesc.Stereo = FALSE;                                           // Stereo rendering disabled
        swapChainDesc.SampleDesc.Count = 1;                                     // No multi-sampling for better performance
        swapChainDesc.SampleDesc.Quality = 0;                                   // No multi-sampling quality
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;            // Render target output usage
        swapChainDesc.BufferCount = FrameCount;                                 // Double buffering (2 frames)
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;                           // Stretch scaling mode
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;               // Efficient flip and discard for DirectX 12
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;                  // No alpha mode specified
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;           // Allow fullscreen mode switching

        // Create the swap chain using the command queue and window handle
        ComPtr<IDXGISwapChain1> swapChain1;
        hr = factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(),                                               // Command queue associated with the swap chain
            hwnd,                                                               // Window handle (HWND)
            &swapChainDesc,                                                     // Swap chain description
            nullptr,                                                            // No fullscreen transition description
            nullptr,                                                            // No restrict to monitor
            &swapChain1                                                         // Output swap chain
        );

        if (FAILED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create swap chain.");
            ThrowError("Failed to create swap chain");
            return;
        }

        // Disable Alt+Enter fullscreen toggle (we handle this manually)
        hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(hr))
        {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to disable Alt+Enter fullscreen toggle.");
#endif
        }

        // Cast to the full DirectX 12 swap chain interface
        hr = swapChain1.As(&m_swapChain);
        if (FAILED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to cast swap chain interface.");
            ThrowError("Failed to cast swap chain interface");
            return;
        }

        // Get the current frame index from the swap chain
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Set swap chain name for debugging
        m_swapChain->SetPrivateData(WKPDID_D3DDebugObjectName,
            sizeof("DX12Renderer_SwapChain") - 1,
            "DX12Renderer_SwapChain");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Swap chain created successfully with frame index: %d", m_frameIndex);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateSwapChain: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create DirectX 12 Descriptor Heaps
//-----------------------------------------
void DX12Renderer::CreateDescriptorHeaps() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating descriptor heaps...");
#endif

    try {
        // Create Render Target View (RTV) descriptor heap for back buffers
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;                                // One descriptor per frame
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;                      // Render target view type
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;                    // Not shader visible
        rtvHeapDesc.NodeMask = 0;                                               // Single GPU node

        HRESULT hr = m_d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap.heap));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create RTV descriptor heap.");
            ThrowError("CreateDescriptorHeap failed for RTV");
            return;
        }

        // Initialize RTV heap properties
        m_rtvHeap.cpuStart = m_rtvHeap.heap->GetCPUDescriptorHandleForHeapStart();
        m_rtvHeap.gpuStart = {}; // RTV heap is not shader visible
        m_rtvHeap.handleIncrementSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_rtvHeap.currentOffset = 0;

        // Set RTV heap name for debugging
        m_rtvHeap.heap->SetName(L"DX12Renderer_RTVHeap");

        // Create Depth Stencil View (DSV) descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;                                         // Only one depth stencil view
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;                      // Depth stencil view type
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;                    // Not shader visible
        dsvHeapDesc.NodeMask = 0;                                               // Single GPU node

        hr = m_d3d12Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap.heap));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create DSV descriptor heap.");
            ThrowError("CreateDescriptorHeap failed for DSV");
            return;
        }

        // Initialize DSV heap properties
        m_dsvHeap.cpuStart = m_dsvHeap.heap->GetCPUDescriptorHandleForHeapStart();
        m_dsvHeap.gpuStart = {}; // DSV heap is not shader visible
        m_dsvHeap.handleIncrementSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_dsvHeap.currentOffset = 0;

        // Set DSV heap name for debugging
        m_dsvHeap.heap->SetName(L"DX12Renderer_DSVHeap");

        // Create CBV/SRV/UAV descriptor heap for textures and constant buffers
        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
        cbvSrvUavHeapDesc.NumDescriptors = MAX_TEXTURE_BUFFERS_3D + 10;          // Textures plus constant buffers
        cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;        // Combined heap type
        cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;    // Shader visible for binding
        cbvSrvUavHeapDesc.NodeMask = 0;                                         // Single GPU node

        hr = m_d3d12Device->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&m_cbvSrvUavHeap.heap));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create CBV/SRV/UAV descriptor heap.");
            ThrowError("CreateDescriptorHeap failed for CBV/SRV/UAV");
            return;
        }

        // Initialize CBV/SRV/UAV heap properties
        m_cbvSrvUavHeap.cpuStart = m_cbvSrvUavHeap.heap->GetCPUDescriptorHandleForHeapStart();
        m_cbvSrvUavHeap.gpuStart = m_cbvSrvUavHeap.heap->GetGPUDescriptorHandleForHeapStart();
        m_cbvSrvUavHeap.handleIncrementSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_cbvSrvUavHeap.currentOffset = 0;

        // Set CBV/SRV/UAV heap name for debugging
        m_cbvSrvUavHeap.heap->SetName(L"DX12Renderer_CBVSRVUAVHeap");

        // Create Sampler descriptor heap for texture sampling
        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
        samplerHeapDesc.NumDescriptors = 10;                                    // Multiple sampler types
        samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;              // Sampler heap type
        samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;      // Shader visible for binding
        samplerHeapDesc.NodeMask = 0;                                           // Single GPU node

        hr = m_d3d12Device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap.heap));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create Sampler descriptor heap.");
            ThrowError("CreateDescriptorHeap failed for Sampler");
            return;
        }

        // Initialize Sampler heap properties
        m_samplerHeap.cpuStart = m_samplerHeap.heap->GetCPUDescriptorHandleForHeapStart();
        m_samplerHeap.gpuStart = m_samplerHeap.heap->GetGPUDescriptorHandleForHeapStart();
        m_samplerHeap.handleIncrementSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        m_samplerHeap.currentOffset = 0;

        // Set Sampler heap name for debugging
        m_samplerHeap.heap->SetName(L"DX12Renderer_SamplerHeap");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: All descriptor heaps created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateDescriptorHeaps: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Render Target Views for Swap Chain Buffers
//-----------------------------------------
void DX12Renderer::CreateRenderTargetViews() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating render target views...");
#endif

    try {
        // Get the RTV descriptor handle starting point
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.cpuStart);

        // Create render target views for each frame buffer
        for (UINT i = 0; i < FrameCount; ++i) {
            // Get the back buffer resource from the swap chain
            HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_frameContexts[i].renderTarget));
            if (FAILED(hr)) {
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to get swap chain buffer %d.", i);
                ThrowError("GetSwapChainBuffer failed");
                return;
            }

            // Create render target view for this buffer
            m_d3d12Device->CreateRenderTargetView(m_frameContexts[i].renderTarget.Get(), nullptr, rtvHandle);

            // Store the RTV handle for this frame
            m_frameContexts[i].rtvHandle = rtvHandle;

            // Set buffer name for debugging
            std::wstring bufferName = L"DX12Renderer_BackBuffer_" + std::to_wstring(i);
            m_frameContexts[i].renderTarget->SetName(bufferName.c_str());

            // Move to the next descriptor handle
            rtvHandle.Offset(1, m_rtvHeap.handleIncrementSize);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Created RTV for frame %d successfully.", i);
#endif
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: All render target views created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateRenderTargetViews: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Depth Stencil Buffer for Depth Testing
//-----------------------------------------
void DX12Renderer::CreateDepthStencilBuffer() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating depth stencil buffer...");
#endif

    try {
        // Describe the depth stencil buffer resource
        D3D12_RESOURCE_DESC depthStencilDesc = {};
        depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;        // 2D texture
        depthStencilDesc.Alignment = 0;                                         // Default alignment
        depthStencilDesc.Width = iOrigWidth;                                    // Match render target width
        depthStencilDesc.Height = iOrigHeight;                                  // Match render target height
        depthStencilDesc.DepthOrArraySize = 1;                                  // Single depth slice
        depthStencilDesc.MipLevels = 1;                                         // Single mip level
        depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                // 24-bit depth, 8-bit stencil
        depthStencilDesc.SampleDesc.Count = 1;                                  // No multisampling
        depthStencilDesc.SampleDesc.Quality = 0;                                // No multisampling quality
        depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;                 // Driver-optimized layout
        depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;       // Allow depth stencil usage

        // Define the clear value for optimal performance
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                      // Match depth buffer format
        clearValue.DepthStencil.Depth = 1.0f;                                   // Clear to maximum depth
        clearValue.DepthStencil.Stencil = 0;                                    // Clear stencil to zero

        // Create the depth stencil buffer resource
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),                  // Default heap for GPU access
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &depthStencilDesc,                                                  // Resource description
            D3D12_RESOURCE_STATE_DEPTH_WRITE,                                   // Initial state for depth writing
            &clearValue,                                                        // Optimal clear value
            IID_PPV_ARGS(&m_depthStencilBuffer)                                 // Output depth stencil buffer
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create depth stencil buffer.");
            ThrowError("CreateDepthStencilBuffer resource failed");
            return;
        }

        // Set depth buffer name for debugging
        m_depthStencilBuffer->SetName(L"DX12Renderer_DepthStencilBuffer");

        // Create depth stencil view descriptor
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                         // Match buffer format
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;                  // 2D texture view
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;                                    // No special flags
        dsvDesc.Texture2D.MipSlice = 0;                                         // Use mip level 0

        // Create the depth stencil view
        m_d3d12Device->CreateDepthStencilView(
            m_depthStencilBuffer.Get(),                                         // Depth stencil resource
            &dsvDesc,                                                           // DSV description
            m_dsvHeap.cpuStart                                                  // DSV heap handle
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Depth stencil buffer created successfully. Size: %dx%d", iOrigWidth, iOrigHeight);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateDepthStencilBuffer: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Command List and Command Allocators
//-----------------------------------------
void DX12Renderer::CreateCommandList() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating command list and allocators...");
#endif

    try {
        // Create command allocators for each frame context
        for (UINT i = 0; i < FrameCount; ++i) {
            HRESULT hr = m_d3d12Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,                                 // Direct command list type for graphics
                IID_PPV_ARGS(&m_frameContexts[i].commandAllocator)              // Output command allocator
            );

            if (FAILED(hr)) {
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create command allocator %d.", i);
                ThrowError("CreateCommandAllocator failed");
                return;
            }

            // Set command allocator name for debugging
            std::wstring allocatorName = L"DX12Renderer_CommandAllocator_" + std::to_wstring(i);
            m_frameContexts[i].commandAllocator->SetName(allocatorName.c_str());

            // Initialize fence value for this frame
            m_frameContexts[i].fenceValue = 0;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Created command allocator %d successfully.", i);
#endif
        }

        // Create the main graphics command list
        HRESULT hr = m_d3d12Device->CreateCommandList(
            0,                                                                  // Single GPU node
            D3D12_COMMAND_LIST_TYPE_DIRECT,                                     // Direct command list for graphics
            m_frameContexts[0].commandAllocator.Get(),                          // Use first frame's allocator initially
            nullptr,                                                            // No initial pipeline state
            IID_PPV_ARGS(&m_commandList)                                        // Output command list
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create graphics command list.");
            ThrowError("CreateCommandList failed");
            return;
        }

        // Set command list name for debugging
        m_commandList->SetName(L"DX12Renderer_MainCommandList");

        // Close the command list initially (will be reset when needed)
        hr = m_commandList->Close();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to close initial command list.");
            ThrowError("CommandList Close failed");
            return;
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Command list and allocators created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateCommandList: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Synchronization Fence
//-----------------------------------------
void DX12Renderer::CreateFence() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating synchronization fence...");
#endif

    try {
        // Create fence for GPU/CPU synchronization
        HRESULT hr = m_d3d12Device->CreateFence(
            0,                                                                  // Initial fence value
            D3D12_FENCE_FLAG_NONE,                                              // No special flags
            IID_PPV_ARGS(&m_fence)                                              // Output fence object
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create synchronization fence.");
            ThrowError("CreateFence failed");
            return;
        }

        // Set fence name for debugging
        m_fence->SetName(L"DX12Renderer_SyncFence");

        // Initialize fence value
        m_fenceValue = 1;

        // Create event handle for fence signaling
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create fence event handle.");
            ThrowError("CreateEvent for fence failed");
            return;
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Synchronization fence created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateFence: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Select Best Available Graphics Adapter
//-----------------------------------------
ComPtr<IDXGIAdapter4> DX12Renderer::SelectBestAdapter()
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Selecting best graphics adapter...");
#endif

    try {
        // Get the window's position for adapter selection
        RECT windowRect;
        GetWindowRect(hwnd, &windowRect);
        POINT centerPoint = {
            (windowRect.left + windowRect.right) / 2,                           // Window center X
            (windowRect.top + windowRect.bottom) / 2                            // Window center Y
        };

        // Create DXGI factory for adapter enumeration
        ComPtr<IDXGIFactory6> factory;
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create DXGI Factory for adapter selection.");
            return nullptr;
        }

        ComPtr<IDXGIAdapter4> bestAdapter = nullptr;
        UINT bestScore = 0;
        UINT adapterIndex = 0;

        // Enumerate all available adapters
        while (true) {
            ComPtr<IDXGIAdapter4> adapter;
            hr = factory->EnumAdapterByGpuPreference(adapterIndex++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(hr))
                continue;

            // Get adapter description
            DXGI_ADAPTER_DESC3 desc;
            adapter->GetDesc3(&desc);

            // Log adapter information
            std::wstring adapterName(desc.Description);
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Found Adapter: %s", adapterName.c_str());
#endif

            // Skip software adapters
            if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Skipping software adapter.");
#endif
                continue;
            }

            // Test DirectX 12 compatibility
            ComPtr<ID3D12Device> testDevice;
            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&testDevice));
            if (FAILED(hr)) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Adapter does not support DirectX 12.");
#endif
                continue;
            }

            // Check if this adapter controls the display where the window is located
            UINT outputIndex = 0;
            ComPtr<IDXGIOutput> output;
            bool controlsWindow = false;

            while (adapter->EnumOutputs(outputIndex++, &output) != DXGI_ERROR_NOT_FOUND) {
                DXGI_OUTPUT_DESC outputDesc;
                output->GetDesc(&outputDesc);

                RECT monitorRect = outputDesc.DesktopCoordinates;
                if (centerPoint.x >= monitorRect.left && centerPoint.x <= monitorRect.right &&
                    centerPoint.y >= monitorRect.top && centerPoint.y <= monitorRect.bottom) {
                    controlsWindow = true;
                    break;
                }

                output.Reset();
            }

            // Calculate adapter score based on various factors
            UINT score = 0;
            if (controlsWindow) score += 10000;                                 // Huge priority for adapters controlling the window
            if (desc.VendorId == 0x10DE) score += 1000;                        // NVIDIA preference
            if (desc.VendorId == 0x1002) score += 900;                         // AMD preference
            if (desc.VendorId == 0x8086) score += 100;                         // Intel as fallback

            // Add VRAM to score (more VRAM = better)
            score += static_cast<UINT>(desc.DedicatedVideoMemory / (1024 * 1024)); // Convert to MB

            // Prefer adapters with hardware support
            if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
                score += 5000;
            }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Adapter score: %d, VRAM: %llu MB",
                score, desc.DedicatedVideoMemory / (1024 * 1024));
#endif

            // Select this adapter if it has the best score so far
            if (score > bestScore) {
                bestScore = score;
                bestAdapter = adapter;
            }
        }

        if (bestAdapter) {
            DXGI_ADAPTER_DESC3 desc;
            bestAdapter->GetDesc3(&desc);
            std::wstring selectedName(desc.Description);
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Selected Adapter: %s (Score: %d)", selectedName.c_str(), bestScore);
#endif
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: No suitable DirectX 12 adapter found.");
        }

        return bestAdapter;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in SelectBestAdapter: %s", errorMsg.c_str());
        return nullptr;
    }
}

//-----------------------------------------
// Create Debug Layer for DirectX 12
//-----------------------------------------
void DX12Renderer::CreateDebugLayer() {
#ifdef _DEBUG
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Setting up debug layer...");
#endif

    try {
        // Set up info queue for detailed debug messages
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_d3d12Device.As(&infoQueue))) {
            // Enable break on severe errors for debugging
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

            // Filter out less important messages to reduce noise
            D3D12_MESSAGE_SEVERITY severities[] = {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            D3D12_MESSAGE_ID denyIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // Common and usually harmless
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // Handled correctly by our code
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // Handled correctly by our code
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;

            infoQueue->PushStorageFilter(&filter);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Debug layer configured successfully.");
#endif
        }
        else {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to configure debug info queue.");
#endif
        }
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateDebugLayer: %s", errorMsg.c_str());
    }
#endif
}

//-----------------------------------------
// Log Adapter Information for Debugging
//-----------------------------------------
void DX12Renderer::LogAdapterInfo(IDXGIAdapter4* adapter) {
    if (!adapter) return;

    try {
        DXGI_ADAPTER_DESC3 desc;
        HRESULT hr = adapter->GetDesc3(&desc);
        if (FAILED(hr)) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to get adapter description.");
#endif
            return;
        }

        std::wstring adapterName(desc.Description);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: === Adapter Information ===");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Description: %s", adapterName.c_str());
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Vendor ID: 0x%04X", desc.VendorId);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Device ID: 0x%04X", desc.DeviceId);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Dedicated Video Memory: %llu MB",
            desc.DedicatedVideoMemory / (1024 * 1024));
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Dedicated System Memory: %llu MB",
            desc.DedicatedSystemMemory / (1024 * 1024));
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Shared System Memory: %llu MB",
            desc.SharedSystemMemory / (1024 * 1024));

        // Log vendor-specific information
        const wchar_t* vendorName = L"Unknown";
        switch (desc.VendorId) {
        case 0x10DE: vendorName = L"NVIDIA"; break;
        case 0x1002: vendorName = L"AMD"; break;
        case 0x8086: vendorName = L"Intel"; break;
        }
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Vendor: %s", vendorName);

        // Log flags
        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Adapter Type: Software");
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Adapter Type: Hardware");
        }

        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: ========================");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in LogAdapterInfo: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Helper Function to Throw Errors with Debug Logging
//-----------------------------------------
void DX12Renderer::ThrowError(const std::string& message) {
    std::wstring wideMessage = std::wstring(message.begin(), message.end());
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: " + wideMessage);
    throw std::runtime_error(message);
}

//-----------------------------------------
// Convert Color Format from uint8_t to float
//-----------------------------------------
XMFLOAT4 DX12Renderer::ConvertColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return {
        r / 255.0f,                                                             // Convert red from 0-255 to 0.0-1.0
        g / 255.0f,                                                             // Convert green from 0-255 to 0.0-1.0
        b / 255.0f,                                                             // Convert blue from 0-255 to 0.0-1.0
        a / 255.0f                                                              // Convert alpha from 0-255 to 0.0-1.0
    };
}

//-----------------------------------------
// Set Renderer Name
//-----------------------------------------
void DX12Renderer::RendererName(std::string sThisName)
{
    sName = sThisName;
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    std::wstring wideName = std::wstring(sThisName.begin(), sThisName.end());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Renderer name set to: %s", wideName.c_str());
#endif
}

//-----------------------------------------
// Create Root Signature for DirectX 12 Pipeline
//-----------------------------------------
void DX12Renderer::CreateRootSignature() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating root signature...");
#endif

    try {
        // Define root parameters for the graphics pipeline
        CD3DX12_ROOT_PARAMETER1 rootParameters[6];

        // Root Parameter 0: Constant Buffer for camera/view matrices (b0)
        rootParameters[DX12_ROOT_PARAM_CONST_BUFFER].InitAsConstantBufferView(
            0,                                                                  // Register b0
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,                             // Data is static during draw
            D3D12_SHADER_VISIBILITY_ALL                                         // Visible to all shader stages
        );

        // Root Parameter 1: Constant Buffer for model lighting (b1)
        rootParameters[DX12_ROOT_PARAM_LIGHT_BUFFER].InitAsConstantBufferView(
            1,                                                                  // Register b1
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,                             // Data is static during draw
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader only
        );

        // Root Parameter 2: Debug Buffer for pixel shader debugging (b2)
        rootParameters[DX12_ROOT_PARAM_DEBUG_BUFFER].InitAsConstantBufferView(
            2,                                                                  // Register b2
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,                             // Data is static during draw
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader only
        );

        // Root Parameter 3: Global Light Buffer (b3)
        rootParameters[DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER].InitAsConstantBufferView(
            3,                                                                  // Register b3
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,                             // Data is static during draw
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader only
        );

        // Root Parameter 4: Material Buffer (b4)
        rootParameters[DX12_ROOT_PARAM_MATERIAL_BUFFER].InitAsConstantBufferView(
            4,                                                                  // Register b4
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,                             // Data is static during draw
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader only
        );

        // Root Parameter 5: Descriptor Table for Textures (t0-t5)
        CD3DX12_DESCRIPTOR_RANGE1 textureRanges[1];
        textureRanges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,                                    // Shader Resource View type
            6,                                                                  // Number of descriptors (t0-t5)
            0,                                                                  // Base shader register (t0)
            0,                                                                  // Register space 0
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC                             // Data is static during draw
        );

        rootParameters[DX12_ROOT_PARAM_ENVIRONMENT_BUFFER].InitAsDescriptorTable(
            1,                                                                  // Number of descriptor ranges
            textureRanges,                                                      // Descriptor ranges array
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader only
        );

        // Define static samplers for texture sampling
        CD3DX12_STATIC_SAMPLER_DESC staticSamplers[3];

        // Static Sampler 0: Linear Sampler (s0)
        staticSamplers[DX12_SAMPLER_LINEAR] = CD3DX12_STATIC_SAMPLER_DESC(
            0,                                                                  // Shader register s0
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,                                    // Linear filtering
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,                                    // Wrap addressing mode U
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,                                    // Wrap addressing mode V
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,                                    // Wrap addressing mode W
            0.0f,                                                               // Mip LOD bias
            16,                                                                 // Max anisotropy
            D3D12_COMPARISON_FUNC_NEVER,                                        // Comparison function
            D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,                             // Border color
            0.0f,                                                               // Min LOD
            D3D12_FLOAT32_MAX,                                                  // Max LOD
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader
        );

        // Static Sampler 1: Point Sampler (s1)
        staticSamplers[DX12_SAMPLER_POINT] = CD3DX12_STATIC_SAMPLER_DESC(
            1,                                                                  // Shader register s1
            D3D12_FILTER_MIN_MAG_MIP_POINT,                                     // Point filtering
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                                   // Clamp addressing mode U
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                                   // Clamp addressing mode V
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                                   // Clamp addressing mode W
            0.0f,                                                               // Mip LOD bias
            1,                                                                  // Max anisotropy
            D3D12_COMPARISON_FUNC_NEVER,                                        // Comparison function
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,                             // Border color
            0.0f,                                                               // Min LOD
            D3D12_FLOAT32_MAX,                                                  // Max LOD
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader
        );

        // Static Sampler 2: Anisotropic Sampler (s2)
        staticSamplers[DX12_SAMPLER_ANISOTROPIC] = CD3DX12_STATIC_SAMPLER_DESC(
            2,                                                                  // Shader register s2
            D3D12_FILTER_ANISOTROPIC,                                           // Anisotropic filtering
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,                                    // Wrap addressing mode U
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,                                    // Wrap addressing mode V
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,                                    // Wrap addressing mode W
            0.0f,                                                               // Mip LOD bias
            16,                                                                 // Max anisotropy
            D3D12_COMPARISON_FUNC_NEVER,                                        // Comparison function
            D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,                             // Border color
            0.0f,                                                               // Min LOD
            D3D12_FLOAT32_MAX,                                                  // Max LOD
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Visible to pixel shader
        );

        // Create the root signature description
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(
            _countof(rootParameters),                                           // Number of root parameters
            rootParameters,                                                     // Root parameters array
            _countof(staticSamplers),                                           // Number of static samplers
            staticSamplers,                                                     // Static samplers array
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |      // Allow input layout
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |            // Deny hull shader access
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |          // Deny domain shader access
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS          // Deny geometry shader access
        );

        // Serialize the root signature
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
        if (FAILED(hr)) {
            if (error) {
                std::string errorMsg(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Root signature serialization failed: %s",
                    std::wstring(errorMsg.begin(), errorMsg.end()).c_str());
            }
            ThrowError("Root signature serialization failed");
            return;
        }

        // Create the root signature
        hr = m_d3d12Device->CreateRootSignature(
            0,                                                                  // Single GPU node
            signature->GetBufferPointer(),                                      // Serialized signature data
            signature->GetBufferSize(),                                         // Serialized signature size
            IID_PPV_ARGS(&m_rootSignature)                                      // Output root signature
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create root signature.");
            ThrowError("CreateRootSignature failed");
            return;
        }

        // Set root signature name for debugging
        m_rootSignature->SetName(L"DX12Renderer_MainRootSignature");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Root signature created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateRootSignature: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Pipeline State Object for DirectX 12 Rendering
//-----------------------------------------
void DX12Renderer::CreatePipelineState() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating pipeline state object...");
#endif

    try {
        // Load and compile shaders first
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> errors;

        // Compile vertex shader
        HRESULT hr = D3DCompileFromFile(
            L"ModelVShader.hlsl",                                               // Shader filename
            nullptr,                                                            // No defines
            D3D_COMPILE_STANDARD_FILE_INCLUDE,                                  // Include handler
            "main",                                                             // Entry point
            "vs_5_1",                                                           // Shader target (DirectX 12 compatible)
            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,                    // Debug flags for development
            0,                                                                  // No effect flags
            &vertexShader,                                                      // Output compiled shader
            &errors                                                             // Output error messages
        );

        if (FAILED(hr)) {
            if (errors) {
                std::string errorMsg(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Vertex shader compilation failed: %s",
                    std::wstring(errorMsg.begin(), errorMsg.end()).c_str());
            }
            ThrowError("Vertex shader compilation failed");
            return;
        }

        // Compile pixel shader
        hr = D3DCompileFromFile(
            L"ModelPShader.hlsl",                                               // Shader filename
            nullptr,                                                            // No defines
            D3D_COMPILE_STANDARD_FILE_INCLUDE,                                  // Include handler
            "main",                                                             // Entry point
            "ps_5_1",                                                           // Shader target (DirectX 12 compatible)
            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,                    // Debug flags for development
            0,                                                                  // No effect flags
            &pixelShader,                                                       // Output compiled shader
            &errors                                                             // Output error messages
        );

        if (FAILED(hr)) {
            if (errors) {
                std::string errorMsg(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Pixel shader compilation failed: %s",
                    std::wstring(errorMsg.begin(), errorMsg.end()).c_str());
            }
            ThrowError("Pixel shader compilation failed");
            return;
        }

        // Define the vertex input layout matching the Model vertex structure
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Create the Graphics Pipeline State Object (PSO) description
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

        // Root signature
        psoDesc.pRootSignature = m_rootSignature.Get();

        // Shader bytecode
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

        // Blend state for alpha blending
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // Sample mask
        psoDesc.SampleMask = UINT_MAX;

        // Rasterizer state
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;                // No culling for debugging models
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;                   // Counter-clockwise winding
        psoDesc.RasterizerState.DepthClipEnable = TRUE;                         // Enable depth clipping
        psoDesc.RasterizerState.MultisampleEnable = FALSE;                      // No multisampling initially
        psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;                  // No line antialiasing initially

        // Depth stencil state
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;                           // Enable depth testing
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;  // Write all depth values
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;       // Less comparison function
        psoDesc.DepthStencilState.StencilEnable = FALSE;                        // Disable stencil testing

        // Input layout
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

        // Primitive topology
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        // Render target formats
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;                     // Match swap chain format
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;                      // Match depth buffer format

        // Sample description
        psoDesc.SampleDesc.Count = 1;                                           // No multisampling
        psoDesc.SampleDesc.Quality = 0;                                         // No multisampling quality

        // Create the pipeline state object
        hr = m_d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create graphics pipeline state.");
            ThrowError("CreateGraphicsPipelineState failed");
            return;
        }

        // Set pipeline state name for debugging
        m_pipelineState->SetName(L"DX12Renderer_MainPipelineState");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Pipeline state object created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreatePipelineState: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Load and Compile Shaders for DirectX 12
//-----------------------------------------
void DX12Renderer::LoadShaders() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Loading and validating shaders...");
#endif

    try {
        // Check if shader files exist
        std::filesystem::path vertexShaderPath = L"ModelVShader.hlsl";
        std::filesystem::path pixelShaderPath = L"ModelPShader.hlsl";

        if (!std::filesystem::exists(vertexShaderPath)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Vertex shader file not found: ModelVShader.hlsl");
        }
        else {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Found vertex shader file: ModelVShader.hlsl");
#endif
        }

        if (!std::filesystem::exists(pixelShaderPath)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Pixel shader file not found: ModelPShader.hlsl");
        }
        else {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Found pixel shader file: ModelPShader.hlsl");
#endif
        }

        // Additional shader validation can be added here
        // For now, the actual compilation happens in CreatePipelineState()

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Shader validation completed.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in LoadShaders: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Constant Buffers for DirectX 12
//-----------------------------------------
void DX12Renderer::CreateConstantBuffers() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating constant buffers...");
#endif

    try {
        // Create camera constant buffer (matches DX11 ConstantBuffer structure)
        UINT constantBufferSize = (sizeof(ConstantBuffer) + 255) & ~255;        // Align to 256 bytes for DirectX 12

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,                                                         // Upload heap for CPU writes
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &bufferDesc,                                                        // Buffer description
            D3D12_RESOURCE_STATE_GENERIC_READ,                                  // Generic read state for upload heap
            nullptr,                                                            // No optimized clear value
            IID_PPV_ARGS(&m_constantBuffer)                                     // Output constant buffer
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create camera constant buffer.");
            ThrowError("CreateConstantBuffer failed for camera");
            return;
        }

        // Set constant buffer name for debugging
        m_constantBuffer->SetName(L"DX12Renderer_CameraConstantBuffer");

        // Create global light buffer (matches DX11 GlobalLightBuffer structure)
        UINT lightBufferSize = (sizeof(GlobalLightBuffer) + 255) & ~255;       // Align to 256 bytes for DirectX 12

        CD3DX12_RESOURCE_DESC lightBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(lightBufferSize);

        hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,                                                         // Upload heap for CPU writes
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &lightBufferDesc,                                                   // Buffer description
            D3D12_RESOURCE_STATE_GENERIC_READ,                                  // Generic read state for upload heap
            nullptr,                                                            // No optimized clear value
            IID_PPV_ARGS(&m_globalLightBuffer)                                  // Output global light buffer
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create global light buffer.");
            ThrowError("CreateConstantBuffer failed for global lights");
            return;
        }

        // Set global light buffer name for debugging
        m_globalLightBuffer->SetName(L"DX12Renderer_GlobalLightBuffer");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Constant buffers created successfully. Camera CB Size: %d, Light CB Size: %d",
            constantBufferSize, lightBufferSize);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateConstantBuffers: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Create Samplers for Texture Sampling
//-----------------------------------------
void DX12Renderer::CreateSamplers() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating texture samplers...");
#endif

    try {
        // Note: We use static samplers in the root signature for better performance
        // This function is for any dynamic samplers that might be needed in the future

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Using static samplers from root signature. No dynamic samplers created.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateSamplers: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Transition Resource State Helper Function
//-----------------------------------------
void DX12Renderer::TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter) {
    // Only transition if states are different
    if (stateBefore == stateAfter) {
        return;
    }

    try {
        // Create resource barrier for state transition
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resource,                                                           // Resource to transition
            stateBefore,                                                        // Current state
            stateAfter                                                          // Target state
        );

        // Add the barrier to the command list
        m_commandList->ResourceBarrier(1, &barrier);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        // Log state transitions for debugging (only in debug builds)
        const wchar_t* beforeStr = L"UNKNOWN";
        const wchar_t* afterStr = L"UNKNOWN";

        // Convert states to readable strings for debugging
        switch (stateBefore) {
        case D3D12_RESOURCE_STATE_RENDER_TARGET: beforeStr = L"RENDER_TARGET"; break;
        case D3D12_RESOURCE_STATE_DEPTH_WRITE: beforeStr = L"DEPTH_WRITE"; break;
        case D3D12_RESOURCE_STATE_PRESENT: beforeStr = L"PRESENT"; break;
        case D3D12_RESOURCE_STATE_COPY_DEST: beforeStr = L"COPY_DEST"; break;
        case D3D12_RESOURCE_STATE_COPY_SOURCE: beforeStr = L"COPY_SOURCE"; break;
        case D3D12_RESOURCE_STATE_GENERIC_READ: beforeStr = L"GENERIC_READ"; break;
        }

        switch (stateAfter) {
        case D3D12_RESOURCE_STATE_RENDER_TARGET: afterStr = L"RENDER_TARGET"; break;
        case D3D12_RESOURCE_STATE_DEPTH_WRITE: afterStr = L"DEPTH_WRITE"; break;
        case D3D12_RESOURCE_STATE_PRESENT: afterStr = L"PRESENT"; break;
        case D3D12_RESOURCE_STATE_COPY_DEST: afterStr = L"COPY_DEST"; break;
        case D3D12_RESOURCE_STATE_COPY_SOURCE: afterStr = L"COPY_SOURCE"; break;
        case D3D12_RESOURCE_STATE_GENERIC_READ: afterStr = L"GENERIC_READ"; break;
        }

        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Resource transition: %s -> %s", beforeStr, afterStr);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in TransitionResource: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Update Constant Buffers with Current Frame Data
//-----------------------------------------
void DX12Renderer::UpdateConstantBuffers() {
    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Updating constant buffers...");
    #endif

    try {
        // Update camera constant buffer
        if (m_constantBuffer) {
            // Map the constant buffer for CPU write
            void* pCBData = nullptr;
            CD3DX12_RANGE readRange(0, 0);                                         // We do not intend to read from this resource on the CPU
            HRESULT hr = m_constantBuffer->Map(0, &readRange, &pCBData);
            if (SUCCEEDED(hr)) {
                // Populate the constant buffer data
                ConstantBuffer cb;
                cb.viewMatrix = myCamera.GetViewMatrix();                           // Get current view matrix from camera
                cb.projectionMatrix = myCamera.GetProjectionMatrix();               // Get current projection matrix from camera
                cb.cameraPosition = myCamera.GetPosition();                         // Get current camera position

                // Copy the data to the mapped buffer
                memcpy(pCBData, &cb, sizeof(ConstantBuffer));

                // Unmap the buffer
                m_constantBuffer->Unmap(0, nullptr);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                XMFLOAT3 camPos = myCamera.GetPosition();
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Camera CB updated. Position: (%.2f, %.2f, %.2f)",
                    camPos.x, camPos.y, camPos.z);
#endif
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to map camera constant buffer.");
            }
        }

        // Update global light buffer
        if (m_globalLightBuffer) {
            // Get all lights from the lights manager
            std::vector<LightStruct> globalLights = lightsManager.GetAllLights();

            void* pLightData = nullptr;
            CD3DX12_RANGE lightReadRange(0, 0);                                     // We do not intend to read from this resource on the CPU
            HRESULT hr = m_globalLightBuffer->Map(0, &lightReadRange, &pLightData);
            if (SUCCEEDED(hr)) {
                // Populate the global light buffer data
                GlobalLightBuffer glb = {};
                glb.numLights = static_cast<int>(globalLights.size());
                if (glb.numLights > MAX_GLOBAL_LIGHTS)
                    glb.numLights = MAX_GLOBAL_LIGHTS;

                // Copy light data to buffer
                for (int i = 0; i < glb.numLights; ++i) {
                    memcpy(&glb.lights[i], &globalLights[i], sizeof(LightStruct));  // Copy the light data

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG_LIGHTING_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"DX12Renderer: Global Light[%d] active=%d intensity=%.2f color=(%.2f %.2f %.2f) range=%.2f type=%d position=(%.2f, %.2f, %.2f)",
                        i, glb.lights[i].active, glb.lights[i].intensity,
                        glb.lights[i].color.x, glb.lights[i].color.y, glb.lights[i].color.z,
                        glb.lights[i].range, glb.lights[i].type,
                        glb.lights[i].position.x, glb.lights[i].position.y, glb.lights[i].position.z);
#endif
                }

                // Copy the data to the mapped buffer
                memcpy(pLightData, &glb, sizeof(GlobalLightBuffer));

                // Unmap the buffer
                m_globalLightBuffer->Unmap(0, nullptr);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Global light buffer updated. Light count: %d", glb.numLights);
#endif
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to map global light buffer.");
            }
        }
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in UpdateConstantBuffers: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Wait for Previous Frame to Complete
//-----------------------------------------
void DX12Renderer::WaitForPreviousFrame() {
    try {
        // Signal fence with current fence value
        const UINT64 fence = m_frameContexts[m_frameIndex].fenceValue;
        HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fence);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to signal fence.");
            return;
        }

        // Update frame index to next frame
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // If the next frame is not ready to be rendered yet, wait until it is ready
        if (m_fence->GetCompletedValue() < m_frameContexts[m_frameIndex].fenceValue) {
            hr = m_fence->SetEventOnCompletion(m_frameContexts[m_frameIndex].fenceValue, m_fenceEvent);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set fence event.");
                return;
            }

            // Wait for the fence event to be signaled
            DWORD waitResult = WaitForSingleObject(m_fenceEvent, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to wait for fence event.");
                return;
            }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Waited for frame %d to complete.", m_frameIndex);
#endif
        }

        // Set fence value for next frame
        m_frameContexts[m_frameIndex].fenceValue = fence + 1;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in WaitForPreviousFrame: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Move to Next Frame
//-----------------------------------------
void DX12Renderer::MoveToNextFrame() {
    try {
        // Assign the current fence value to the current frame
        m_frameContexts[m_frameIndex].fenceValue = m_fenceValue;

        // Signal the fence from the GPU side
        HRESULT hr = m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to signal fence for next frame.");
            return;
        }

        // Update the frame index
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // If the next frame is not ready to be rendered yet, wait until it is ready
        if (m_fence->GetCompletedValue() < m_frameContexts[m_frameIndex].fenceValue) {
            hr = m_fence->SetEventOnCompletion(m_frameContexts[m_frameIndex].fenceValue, m_fenceEvent);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set fence event for next frame.");
                return;
            }

            // Wait for the fence event
            DWORD waitResult = WaitForSingleObject(m_fenceEvent, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to wait for fence event in MoveToNextFrame.");
                return;
            }
        }

        // Increment the fence value for the next frame
        m_fenceValue++;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Moved to frame %d, fence value: %llu", m_frameIndex, m_fenceValue);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in MoveToNextFrame: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Reset Command List for New Frame
//-----------------------------------------
void DX12Renderer::ResetCommandList() {
    try {
        // Reset the command allocator for the current frame
        HRESULT hr = m_frameContexts[m_frameIndex].commandAllocator->Reset();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to reset command allocator.");
            return;
        }

        // Reset the command list with the current frame's allocator and pipeline state
        hr = m_commandList->Reset(m_frameContexts[m_frameIndex].commandAllocator.Get(), m_pipelineState.Get());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to reset command list.");
            return;
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Command list reset for frame %d.", m_frameIndex);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in ResetCommandList: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Close Command List After Recording Commands
//-----------------------------------------
void DX12Renderer::CloseCommandList() {
    try {
        // Close the command list to finalize command recording
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to close command list.");
            return;
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Command list closed successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CloseCommandList: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Execute Command List on GPU
//-----------------------------------------
void DX12Renderer::ExecuteCommandList() {
    try {
        // Execute the command list on the GPU
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Command list executed on GPU.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in ExecuteCommandList: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Present Frame to Display
//-----------------------------------------
void DX12Renderer::PresentFrame() {
    try {
        // Present the frame to the display
        HRESULT hr = m_swapChain->Present(config.myConfig.enableVSync ? 1 : 0, 0);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to present frame.");
            return;
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Frame presented successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in PresentFrame: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Populate Command List with Rendering Commands
//-----------------------------------------
void DX12Renderer::PopulateCommandList() {
    try {
        // Set the graphics root signature
        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

        // Set descriptor heaps
        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        // Set the viewport and scissor rect
        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = static_cast<float>(iOrigWidth);
        viewport.Height = static_cast<float>(iOrigHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissorRect = {};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = iOrigWidth;
        scissorRect.bottom = iOrigHeight;

        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissorRect);

        // Transition the render target from present to render target state
        TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Get render target and depth stencil handles
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_frameContexts[m_frameIndex].rtvHandle;
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);

        // Set render targets
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        // Clear the render target and depth stencil
        const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };                 // Black clear color
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Set constant buffer views
        if (m_constantBuffer) {
            m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER,
                m_constantBuffer->GetGPUVirtualAddress());
        }

        if (m_globalLightBuffer) {
            m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER,
                m_globalLightBuffer->GetGPUVirtualAddress());
        }

        // Set primitive topology
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Render 3D models if loading is complete
        if (threadManager.threadVars.bLoaderTaskFinished.load()) {
            for (int i = 0; i < MAX_MODELS; ++i) {
                if (scene.scene_models[i].m_isLoaded) {
                    // Model rendering will be implemented in the next step
                    // For now, we just set up the basic rendering state
                }
            }
        }

        // Transition the render target back to present state
        TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Command list populated with rendering commands.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in PopulateCommandList: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Wait for GPU to Finish All Operations
//-----------------------------------------
void DX12Renderer::WaitForGPUToFinish() {
    try {
        // Signal the fence
        const UINT64 fenceValue = m_fenceValue;
        HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to signal fence in WaitForGPUToFinish.");
            return;
        }

        // Increment the fence value
        m_fenceValue++;

        // Wait until the fence has been processed
        if (m_fence->GetCompletedValue() < fenceValue) {
            hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set fence event in WaitForGPUToFinish.");
                return;
            }

            DWORD waitResult = WaitForSingleObject(m_fenceEvent, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to wait for GPU completion.");
                return;
            }
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: GPU operations completed successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in WaitForGPUToFinish: %s", errorMsg.c_str());
        throw;
    }
}

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
//-----------------------------------------
// Set Debug Mode for Pixel Shader Debugging
//-----------------------------------------
void DX12Renderer::SetDebugMode(int mode) {
    try {
        // Create debug buffer data
        DebugBuffer dbg = {};
        dbg.debugMode = mode;

        // For DirectX 12, we would need to create a dedicated debug constant buffer
        // and update it through the command list. This is more complex than DirectX 11
        // For now, we'll log the debug mode change

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Debug mode set to: %d", mode);
#endif

        // TODO: Implement actual debug buffer update for DirectX 12
        // This would require creating a separate debug constant buffer and binding it
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in SetDebugMode: %s", errorMsg.c_str());
        throw;
    }
}
#endif

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
//-----------------------------------------
// Test Draw Triangle for Pipeline Validation
//-----------------------------------------
void DX12Renderer::TestDrawTriangle() {
    try {
        // Simple triangle test for DirectX 12 pipeline validation
        // This function would create a simple triangle vertex buffer and render it
        // Implementation would be similar to DX11 but using DirectX 12 command lists

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Test triangle rendering requested.");
#endif

        // TODO: Implement DirectX 12 triangle test
        // This would require creating vertex buffers, setting up input layout, and drawing
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in TestDrawTriangle: %s", errorMsg.c_str());
        throw;
    }
}
#endif

//-----------------------------------------
// Initialize DirectX 11 on DirectX 12 Compatibility Layer
//-----------------------------------------
bool DX12Renderer::InitializeDX11On12Compatibility() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Initializing DirectX 11 on 12 compatibility layer...");
#endif

    try {
        // Check if DirectX 12 device is available
        if (!m_d3d12Device) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 12 device not available for compatibility layer.");
            m_dx11Dx12Compat.bDX11Available = false;
            m_dx11Dx12Compat.bDX12Available = false;
            return false;
        }

        // Create DirectX 11 device and context
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;                  // Required for Direct2D interop
#ifdef _DEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;                             // Enable debug layer in debug builds
#endif

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,                                             // Prefer DirectX 11.1
            D3D_FEATURE_LEVEL_11_0                                              // Fallback to DirectX 11.0
        };

        D3D_FEATURE_LEVEL selectedFeatureLevel;

        // Create DirectX 11 device for compatibility
        HRESULT hr = D3D11CreateDevice(
            nullptr,                                                            // Use default adapter
            D3D_DRIVER_TYPE_HARDWARE,                                           // Hardware driver
            nullptr,                                                            // No software module
            creationFlags,                                                      // Creation flags
            featureLevels,                                                      // Feature levels array
            _countof(featureLevels),                                            // Number of feature levels
            D3D11_SDK_VERSION,                                                  // SDK version
            &m_dx11Dx12Compat.dx11Device,                                       // Output DirectX 11 device
            &selectedFeatureLevel,                                              // Selected feature level
            &m_dx11Dx12Compat.dx11Context                                       // Output DirectX 11 context
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to create DirectX 11 device for compatibility.");
            m_dx11Dx12Compat.bDX11Available = false;
            return false;
        }

        // Set DirectX 11 device name for debugging
        m_dx11Dx12Compat.dx11Device->SetPrivateData(WKPDID_D3DDebugObjectName,
            sizeof("DX12Renderer_DX11CompatDevice") - 1,
            "DX12Renderer_DX11CompatDevice");

        // Set DirectX 11 context name for debugging  
        m_dx11Dx12Compat.dx11Context->SetPrivateData(WKPDID_D3DDebugObjectName,
            sizeof("DX12Renderer_DX11CompatContext") - 1,
            "DX12Renderer_DX11CompatContext");

        // Create DirectX 11 on 12 device for interoperability
        hr = D3D11On12CreateDevice(
            m_d3d12Device.Get(),                                                // DirectX 12 device
            creationFlags,                                                      // Creation flags
            featureLevels,                                                      // Feature levels array
            _countof(featureLevels),                                            // Number of feature levels
            reinterpret_cast<IUnknown**>(m_commandQueue.GetAddressOf()),       // Command queue array
            1,                                                                  // Number of command queues
            0,                                                                  // Node mask
            &m_dx11Dx12Compat.dx11On12Device,                                   // Output DirectX 11 on 12 device
            &m_dx11Dx12Compat.dx11Context,                                      // Output DirectX 11 context
            &selectedFeatureLevel                                               // Selected feature level
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to create DirectX 11 on 12 device.");
            // Continue without DirectX 11 on 12 interop, but keep basic DirectX 11 support
            m_dx11Dx12Compat.dx11On12Device = nullptr;
        }
        else {
            // Set DirectX 11 on 12 device name for debugging
            m_dx11Dx12Compat.dx11On12Device->SetPrivateData(WKPDID_D3DDebugObjectName,
                sizeof("DX12Renderer_DX11On12Device") - 1,
                "DX12Renderer_DX11On12Device");

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: DirectX 11 on 12 device created successfully.");
#endif
        }

        // Initialize Direct2D factory
        D2D1_FACTORY_OPTIONS d2dFactoryOptions = {};
#ifdef _DEBUG
        d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;            // Enable debug information
#endif

        hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_MULTI_THREADED,                                   // Multi-threaded factory for thread safety
            __uuidof(ID2D1Factory3),                                            // Factory interface
            &d2dFactoryOptions,                                                 // Factory options
            reinterpret_cast<void**>(m_d2dFactory.GetAddressOf())               // Output factory
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create Direct2D factory.");
            CleanupDX11On12Compatibility();
            return false;
        }

        // Initialize DirectWrite factory
        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,                                         // Shared factory
            __uuidof(IDWriteFactory),                                           // Factory interface
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())        // Output factory
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create DirectWrite factory.");
            CleanupDX11On12Compatibility();
            return false;
        }

        // Get DXGI device from DirectX 11 device for Direct2D device creation
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = m_dx11Dx12Compat.dx11Device.As(&dxgiDevice);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get DXGI device from DirectX 11 device.");
            CleanupDX11On12Compatibility();
            return false;
        }

        // Create Direct2D device
        hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create Direct2D device.");
            CleanupDX11On12Compatibility();
            return false;
        }

        // Create Direct2D device context
        hr = m_d2dDevice->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE,                                   // No special options
            &m_d2dContext                                                       // Output device context
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create Direct2D device context.");
            CleanupDX11On12Compatibility();
            return false;
        }

        // Mark compatibility layer as available
        m_dx11Dx12Compat.bDX11Available = true;
        m_dx11Dx12Compat.bDX12Available = true;
        m_dx11Dx12Compat.bUsingDX11Fallback = false;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        const wchar_t* featureLevelStr = L"Unknown";
        switch (selectedFeatureLevel) {
        case D3D_FEATURE_LEVEL_11_1: featureLevelStr = L"11.1"; break;
        case D3D_FEATURE_LEVEL_11_0: featureLevelStr = L"11.0"; break;
        case D3D_FEATURE_LEVEL_10_1: featureLevelStr = L"10.1"; break;
        case D3D_FEATURE_LEVEL_10_0: featureLevelStr = L"10.0"; break;
        }

        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: DirectX 11-12 compatibility layer initialized successfully. Feature Level: %s", featureLevelStr);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: DX11 Available: %s, DX12 Available: %s, DX11on12: %s",
            m_dx11Dx12Compat.bDX11Available ? L"Yes" : L"No",
            m_dx11Dx12Compat.bDX12Available ? L"Yes" : L"No",
            m_dx11Dx12Compat.dx11On12Device ? L"Yes" : L"No");
#endif

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in InitializeDX11On12Compatibility: %s", errorMsg.c_str());
        CleanupDX11On12Compatibility();
        return false;
    }
}

//-----------------------------------------
// Cleanup DirectX 11-12 Compatibility Layer
//-----------------------------------------
void DX12Renderer::CleanupDX11On12Compatibility() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Cleaning up DirectX 11-12 compatibility layer...");
#endif

    try {
        // Release Direct2D resources
        if (m_d2dContext) {
            m_d2dContext->SetTarget(nullptr);                                   // Clear render target
            m_d2dContext.Reset();                                               // Release device context
        }

        if (m_d2dDevice) {
            m_d2dDevice.Reset();                                                // Release Direct2D device
        }

        if (m_d2dFactory) {
            m_d2dFactory.Reset();                                               // Release Direct2D factory
        }

        if (m_dwriteFactory) {
            m_dwriteFactory.Reset();                                            // Release DirectWrite factory
        }

        // Release DirectX 11 on 12 resources
        if (m_dx11Dx12Compat.dx11On12Device) {
            m_dx11Dx12Compat.dx11On12Device.Reset();                            // Release DirectX 11 on 12 device
        }

        // Release DirectX 11 compatibility resources
        if (m_dx11Dx12Compat.dx11Context) {
            m_dx11Dx12Compat.dx11Context->ClearState();                         // Clear DirectX 11 state
            m_dx11Dx12Compat.dx11Context->Flush();                              // Flush pending operations
            m_dx11Dx12Compat.dx11Context.Reset();                               // Release DirectX 11 context
        }

        if (m_dx11Dx12Compat.dx11Device) {
            m_dx11Dx12Compat.dx11Device.Reset();                                // Release DirectX 11 device
        }

        // Reset compatibility flags
        m_dx11Dx12Compat.bDX11Available = false;
        m_dx11Dx12Compat.bDX12Available = false;
        m_dx11Dx12Compat.bUsingDX11Fallback = false;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: DirectX 11-12 compatibility layer cleaned up successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CleanupDX11On12Compatibility: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Check if DirectX 11 Compatibility is Available
//-----------------------------------------
bool DX12Renderer::IsDX11CompatibilityAvailable() const {
    bool isAvailable = m_dx11Dx12Compat.bDX11Available && m_dx11Dx12Compat.dx11Device && m_dx11Dx12Compat.dx11Context;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: DirectX 11 compatibility check: %s", isAvailable ? L"Available" : L"Not Available");
#endif

    return isAvailable;
}

//-----------------------------------------
// Get DirectX 11 Compatibility Device
//-----------------------------------------
ComPtr<ID3D11Device> DX12Renderer::GetDX11CompatDevice() const {
    if (!IsDX11CompatibilityAvailable()) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: DirectX 11 compatibility device requested but not available.");
#endif
        return nullptr;
    }

    return m_dx11Dx12Compat.dx11Device;
}

//-----------------------------------------
// Get DirectX 11 Compatibility Context
//-----------------------------------------
ComPtr<ID3D11DeviceContext> DX12Renderer::GetDX11CompatContext() const {
    if (!IsDX11CompatibilityAvailable()) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: DirectX 11 compatibility context requested but not available.");
#endif
        return nullptr;
    }

    return m_dx11Dx12Compat.dx11Context;
}

//-----------------------------------------
// Initialize Main DirectX 12 Renderer
//-----------------------------------------
void DX12Renderer::Initialize(HWND hwnd, HINSTANCE hInstance) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Initializing DirectX 12 renderer...");
#endif

    try {
        // Set the Renderer Name
        RendererName(RENDERER_NAME_DX12);
        iOrigWidth = winMetrics.clientWidth;
        iOrigHeight = winMetrics.clientHeight;

        // Initialize DirectX 12 core components in proper order
        CreateDevice();                                                         // Create DirectX 12 device
        CreateCommandQueue();                                                   // Create command queue
        CreateSwapChain(hwnd);                                                  // Create swap chain
        CreateDescriptorHeaps();                                                // Create descriptor heaps
        CreateRenderTargetViews();                                              // Create render target views
        CreateDepthStencilBuffer();                                             // Create depth stencil buffer
        CreateCommandList();                                                    // Create command list and allocators
        CreateFence();                                                          // Create synchronization fence
        CreateRootSignature();                                                  // Create root signature
        CreatePipelineState();                                                  // Create pipeline state object
        CreateConstantBuffers();                                                // Create constant buffers
        CreateSamplers();                                                       // Create samplers
        LoadShaders();                                                          // Load and validate shaders

        // Initialize DirectX 11-12 compatibility layer for 2D rendering
        bool compatibilitySuccess = InitializeDX11On12Compatibility();
        if (!compatibilitySuccess) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: DirectX 11-12 compatibility layer failed to initialize. 2D rendering may be limited.");
        }

        // Initialize our Camera to default values
        if (!threadManager.threadVars.bIsResizing.load())
        {
            myCamera.SetupDefaultCamera(iOrigWidth, iOrigHeight);
        }

        // Disable mouse cursor for gaming experience
        sysUtils.DisableMouseCursor();

        // Mark renderer as initialized
        bIsInitialized.store(true);

        if (threadManager.threadVars.bIsResizing.load())
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: DirectX 12 Rendering Engine Initialised and Activated.");
        }
        else
        {
            // We are resizing the window, so restart the loading sequence
            threadManager.ResumeThread(THREAD_LOADER);
        }

        // Clear resizing flag
        threadManager.threadVars.bIsResizing.store(false);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Initialization completed successfully. Resolution: %dx%d", iOrigWidth, iOrigHeight);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Initialize: %s", errorMsg.c_str());

        // Cleanup on initialization failure
        Cleanup();
        throw;
    }
}

//-----------------------------------------
// Start Renderer Threads
//-----------------------------------------
bool DX12Renderer::StartRendererThreads()
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Starting renderer threads...");
#endif

    bool result = true;
    try
    {
        // Initialize and Start the Loader Thread
        threadManager.SetThread(THREAD_LOADER, [this]() { LoaderTaskThread(); }, true);
        threadManager.StartThread(THREAD_LOADER);

        // Initialize & start the renderer thread
#ifdef RENDERER_IS_THREAD
        threadManager.SetThread(THREAD_RENDERER, [this]() { RenderFrame(); }, true);
        threadManager.StartThread(THREAD_RENDERER);
#endif

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Renderer threads started successfully.");
#endif
    }
    catch (const std::exception& e)
    {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Exception in StartRendererThreads: %s", errorMsg.c_str());
        result = false;
    }

    return result;
}

//-----------------------------------------
// Loader Task Thread for DirectX 12
//-----------------------------------------
void DX12Renderer::LoaderTaskThread() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Loader task thread started...");
#endif

    try {
        // Set thread name for debugging
        std::string threadName = threadManager.getThreadName(THREAD_LOADER);

        // Wait for renderer to be initialized before starting loading
        while (!bIsInitialized.load() && !threadManager.threadVars.bIsShuttingDown.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (threadManager.threadVars.bIsShuttingDown.load()) {
            return;
        }

        // Load all known textures
        bool texturesLoaded = LoadAllKnownTextures();
        if (!texturesLoaded) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to load textures in loader thread.");
        }

        // Load models and other resources here
        // This will be implemented in subsequent steps

        // Mark loading as finished
        threadManager.threadVars.bLoaderTaskFinished.store(true);

        // Clear resizing flag if it was set
        if (wasResizing.load()) {
            wasResizing.store(false);
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Loader task thread completed successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in LoaderTaskThread: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Resume Loader Thread
//-----------------------------------------
void DX12Renderer::ResumeLoader(bool isResizing) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Resuming loader thread. Is resizing: %s", isResizing ? L"Yes" : L"No");
#endif

    try {
        // Set resizing flag if needed
        if (isResizing)
            wasResizing.store(true);

        // Clear busy flags
        D2DBusy.store(false);
        threadManager.threadVars.bLoaderTaskFinished.store(false);

        // Get current thread status
        ThreadStatus tstat = threadManager.GetThreadStatus(THREAD_LOADER);

        // Resume or restart loader thread based on current status
        std::thread resumeLoaderThread([this, tstat]() {
            try
            {
                if (tstat == ThreadStatus::Running || tstat == ThreadStatus::Paused)
                {
                    threadManager.ResumeThread(THREAD_LOADER);
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: THREAD LOADING System Resumed.");
#endif
                }
                else if (tstat == ThreadStatus::Stopped || tstat == ThreadStatus::Terminated)
                {
                    // Set the thread with the correct handler lambda
                    threadManager.SetThread(THREAD_LOADER, [this]() {
                        this->LoaderTaskThread(); // safely bound to DX12Renderer instance
                        }, true);

                    threadManager.StartThread(THREAD_LOADER);
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: THREAD LOADING System Restarted.");
#endif
                }
            }
            catch (const std::exception& e)
            {
                std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Exception during thread resume: %s", errorMsg.c_str());
            }
            });

        resumeLoaderThread.detach(); // Properly detach the thread so no crash occurs
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in ResumeLoader: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Cleanup DirectX 12 Renderer Resources
//-----------------------------------------
void DX12Renderer::Cleanup() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Cleaning up DirectX 12 renderer...");
#endif

    if (bHasCleanedUp) {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Cleanup already performed, skipping.");
        #endif
        return;
    }

    try {
        // Synchronize Thread Closures
        threadManager.TerminateThread(THREAD_LOADER);

        #ifdef RENDERER_IS_THREAD
            // Ensure the renderer finishes first!
            while (threadManager.threadVars.bIsRendering.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Small delay to prevent CPU spinning
            }

            // Now terminate the Renderer Thread.
            threadManager.TerminateThread(THREAD_RENDERER);
        #endif

        // Wait for GPU to finish all operations before cleanup
        if (m_d3d12Device && m_commandQueue && m_fence) {
            WaitForGPUToFinish();
        }

        // Clean up the Thread Manager
        threadManager.Cleanup();

        // Clean up DirectX 11-12 compatibility layer
        CleanupDX11On12Compatibility();

        // Release texture resources
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; i++) {
            if (m_d3d12Textures[i]) {
                m_d3d12Textures[i].Reset();
                m_d3d12Textures[i] = nullptr;
            }
        }

        for (int i = 0; i < MAX_TEXTURE_BUFFERS; i++) {
            if (m_d2dTextures[i]) {
                m_d2dTextures[i].Reset();
                m_d2dTextures[i] = nullptr;
            }
        }

        // Destroy Base Models
        for (int i = 0; i < MAX_MODELS; ++i)
        {
            models[i].DestroyModel();  // Force destroy global base models BEFORE anything reloads
        }

        // Destroy our Scene Models
        scene.CleanUp();

        // Release DirectX 12 resources in reverse order of creation
        if (m_commandList) {
            m_commandList.Reset();
        }

        // Release frame contexts
        for (UINT i = 0; i < FrameCount; ++i) {
            if (m_frameContexts[i].commandAllocator) {
                m_frameContexts[i].commandAllocator.Reset();
            }
            if (m_frameContexts[i].renderTarget) {
                m_frameContexts[i].renderTarget.Reset();
            }
            m_frameContexts[i].fenceValue = 0;
        }

        // Release pipeline resources
        if (m_pipelineState) {
            m_pipelineState.Reset();
        }

        if (m_rootSignature) {
            m_rootSignature.Reset();
        }

        // Release buffer resources
        if (m_constantBuffer) {
            m_constantBuffer.Reset();
        }

        if (m_globalLightBuffer) {
            m_globalLightBuffer.Reset();
        }

        if (m_depthStencilBuffer) {
            m_depthStencilBuffer.Reset();
        }

        // Release descriptor heaps
        if (m_rtvHeap.heap) {
            m_rtvHeap.heap.Reset();
        }

        if (m_dsvHeap.heap) {
            m_dsvHeap.heap.Reset();
        }

        if (m_cbvSrvUavHeap.heap) {
            m_cbvSrvUavHeap.heap.Reset();
        }

        if (m_samplerHeap.heap) {
            m_samplerHeap.heap.Reset();
        }

        // Release synchronization objects
        if (m_fenceEvent) {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }

        if (m_fence) {
            m_fence.Reset();
        }

        // Release core DirectX 12 objects
        if (m_swapChain) {
            m_swapChain.Reset();
        }

        if (m_commandQueue) {
            m_commandQueue.Reset();
        }

        if (m_d3d12Device) {
            m_d3d12Device.Reset();
        }

        // Re-enable mouse cursor
        sysUtils.EnableMouseCursor();

        // Mark cleanup as completed
        bHasCleanedUp = true;

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: DirectX 12 renderer successfully cleaned up.");
        #endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Cleanup: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Load Texture for DirectX 12 (2D and 3D)
//-----------------------------------------
bool DX12Renderer::LoadTexture(int textureIndex, const std::wstring& filename, bool is2D) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Loading texture %d from file: %s (2D: %s)",
        textureIndex, filename.c_str(), is2D ? L"Yes" : L"No");
#endif

    try {
        // Validate texture index
        if (is2D && (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid 2D texture index: %d", textureIndex);
            return false;
        }

        if (!is2D && (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid 3D texture index: %d", textureIndex);
            return false;
        }

        if (is2D) {
            // For 2D textures, use DirectX 11 compatibility layer with Direct2D
            if (!IsDX11CompatibilityAvailable()) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility not available for 2D texture loading.");
                return false;
            }

            // Create WIC factory for image loading
            ComPtr<IWICImagingFactory> wicFactory;
            HRESULT hr = CoCreateInstance(
                CLSID_WICImagingFactory,                                        // WIC factory class ID
                nullptr,                                                        // No aggregation
                CLSCTX_INPROC_SERVER,                                           // In-process server
                IID_PPV_ARGS(&wicFactory)                                       // Output WIC factory
            );

            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create WIC factory for 2D texture.");
                return false;
            }

            // Create decoder for the image file
            ComPtr<IWICBitmapDecoder> decoder;
            hr = wicFactory->CreateDecoderFromFilename(
                filename.c_str(),                                               // Image filename
                nullptr,                                                        // No vendor preference
                GENERIC_READ,                                                   // Read access
                WICDecodeMetadataCacheOnLoad,                                   // Cache metadata on load
                &decoder                                                        // Output decoder
            );

            if (FAILED(hr)) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create WIC decoder for file: %s", filename.c_str());
                return false;
            }

            // Get the first frame from the image
            ComPtr<IWICBitmapFrameDecode> frame;
            hr = decoder->GetFrame(0, &frame);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get WIC frame from image.");
                return false;
            }

            // Create format converter for Direct2D compatibility
            ComPtr<IWICFormatConverter> converter;
            hr = wicFactory->CreateFormatConverter(&converter);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create WIC format converter.");
                return false;
            }

            // Initialize the converter to BGRA format for Direct2D
            hr = converter->Initialize(
                frame.Get(),                                                    // Source frame
                GUID_WICPixelFormat32bppPBGRA,                                  // Target format (premultiplied BGRA)
                WICBitmapDitherTypeNone,                                        // No dithering
                nullptr,                                                        // No palette
                0.0f,                                                           // No alpha threshold
                WICBitmapPaletteTypeCustom                                      // Custom palette type
            );
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to initialize WIC format converter.");
                return false;
            }

            // Create Direct2D bitmap from WIC bitmap
            ComPtr<ID2D1Bitmap> d2dBitmap;
            hr = m_d2dContext->CreateBitmapFromWicBitmap(converter.Get(), &d2dBitmap);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create Direct2D bitmap from WIC bitmap.");
                return false;
            }

            // Store the 2D texture
            m_d2dTextures[textureIndex] = d2dBitmap;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            D2D1_SIZE_F bitmapSize = d2dBitmap->GetSize();
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D texture %d loaded successfully. Size: %.0fx%.0f",
                textureIndex, bitmapSize.width, bitmapSize.height);
#endif
        }
        else {
            // For 3D textures, use native DirectX 12 resource loading
            // Open the DDS file for DirectX 12 native loading
            HANDLE file = CreateFileW(
                filename.c_str(),                                               // Filename
                GENERIC_READ,                                                   // Read access
                FILE_SHARE_READ,                                                // Allow shared reading
                nullptr,                                                        // Default security
                OPEN_EXISTING,                                                  // File must exist
                FILE_ATTRIBUTE_NORMAL,                                          // Normal file attributes
                nullptr                                                         // No template file
            );

            if (file == INVALID_HANDLE_VALUE) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to open 3D texture file: %s", filename.c_str());
                return false;
            }

            // Get file size for buffer allocation
            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(file, &fileSize)) {
                CloseHandle(file);
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get 3D texture file size.");
                return false;
            }

            // Read file data into memory
            std::vector<BYTE> fileData(fileSize.LowPart);
            DWORD bytesRead;
            if (!ReadFile(file, fileData.data(), fileSize.LowPart, &bytesRead, nullptr) || bytesRead != fileSize.LowPart) {
                CloseHandle(file);
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to read 3D texture file data.");
                return false;
            }
            CloseHandle(file);

            // Validate DDS magic number
            DWORD magicNumber = *reinterpret_cast<DWORD*>(fileData.data());
            if (magicNumber != MAKEFOURCC('D', 'D', 'S', ' ')) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid DDS file format.");
                return false;
            }

            // Validate file size
            if (fileData.size() < (sizeof(DDS_HEADER) + sizeof(DWORD))) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DDS file is too small.");
                return false;
            }

            // Parse DDS header
            DDS_HEADER* header = reinterpret_cast<DDS_HEADER*>(fileData.data() + sizeof(DWORD));

            // Determine DXGI format from DDS header
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            if (header->ddspf.dwFourCC == MAKEFOURCC('D', 'X', 'T', '1')) {
                format = DXGI_FORMAT_BC1_UNORM;
            }
            else if (header->ddspf.dwFourCC == MAKEFOURCC('D', 'X', 'T', '3')) {
                format = DXGI_FORMAT_BC2_UNORM;
            }
            else if (header->ddspf.dwFourCC == MAKEFOURCC('D', 'X', 'T', '5')) {
                format = DXGI_FORMAT_BC3_UNORM;
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Unsupported DDS format.");
                return false;
            }

            // Calculate texture size and create upload buffer
            UINT textureWidth = header->dwWidth;
            UINT textureHeight = header->dwHeight;
            UINT mipLevels = header->dwMipMapCount ? header->dwMipMapCount : 1;

            // Create texture resource description
            D3D12_RESOURCE_DESC textureDesc = {};
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;         // 2D texture
            textureDesc.Alignment = 0;                                          // Default alignment
            textureDesc.Width = textureWidth;                                   // Texture width
            textureDesc.Height = textureHeight;                                 // Texture height
            textureDesc.DepthOrArraySize = 1;                                   // Single texture
            textureDesc.MipLevels = mipLevels;                                  // Mip levels from DDS
            textureDesc.Format = format;                                        // Format from DDS
            textureDesc.SampleDesc.Count = 1;                                   // No multisampling
            textureDesc.SampleDesc.Quality = 0;                                 // No multisampling quality
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;                  // Driver-optimized layout
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;                       // No special flags

            // Create the texture resource
            CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
            HRESULT hr = m_d3d12Device->CreateCommittedResource(
                &defaultHeapProps,                                              // Default heap for GPU access
                D3D12_HEAP_FLAG_NONE,                                           // No special heap flags
                &textureDesc,                                                   // Texture description
                D3D12_RESOURCE_STATE_COPY_DEST,                                 // Initial state for copying
                nullptr,                                                        // No optimized clear value
                IID_PPV_ARGS(&m_d3d12Textures[textureIndex])                   // Output texture resource
            );

            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create 3D texture resource.");
                return false;
            }

            // Set texture name for debugging
            std::wstring textureName = L"DX12Renderer_3DTexture_" + std::to_wstring(textureIndex);
            m_d3d12Textures[textureIndex]->SetName(textureName.c_str());

            // TODO: Implement texture data upload using copy commands
            // This requires creating an upload buffer and copying texture data
            // For now, we'll mark the texture as created but not uploaded

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 3D texture %d created successfully. Size: %dx%d, Mips: %d",
                textureIndex, textureWidth, textureHeight, mipLevels);
#endif
        }

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in LoadTexture: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Unload Texture for DirectX 12
//-----------------------------------------
void DX12Renderer::UnloadTexture(int textureIndex, bool is2D) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Unloading texture %d (2D: %s)", textureIndex, is2D ? L"Yes" : L"No");
#endif

    try {
        if (is2D) {
            if (textureIndex >= 0 && textureIndex < MAX_TEXTURE_BUFFERS) {
                if (m_d2dTextures[textureIndex]) {
                    m_d2dTextures[textureIndex].Reset();
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D texture %d unloaded successfully.", textureIndex);
#endif
                }
            }
        }
        else {
            if (textureIndex >= 0 && textureIndex < MAX_TEXTURE_BUFFERS_3D) {
                if (m_d3d12Textures[textureIndex]) {
                    m_d3d12Textures[textureIndex].Reset();
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 3D texture %d unloaded successfully.", textureIndex);
#endif
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in UnloadTexture: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Blit 2D Object at Position
//-----------------------------------------
void DX12Renderer::Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Blitting 2D object %d at position (%d, %d)", static_cast<int>(iIndex), iX, iY);
#endif

    try {
        // Check if the object index is valid
        if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid 2D object index: %d", static_cast<int>(iIndex));
            return;
        }

        // Check if DirectX 11 compatibility is available for 2D rendering
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility or Direct2D context not available for 2D blitting.");
            return;
        }

        // Check if the texture exists
        if (!m_d2dTextures[int(iIndex)]) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 2D texture %d not loaded for blitting.", static_cast<int>(iIndex));
            return;
        }

        // Get the size of the bitmap
        D2D1_SIZE_F bitmapSize = m_d2dTextures[int(iIndex)]->GetSize();

        // Define the destination rectangle where the bitmap will be drawn
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iX),                                             // Left position
            static_cast<float>(iY),                                             // Top position
            static_cast<float>(iX) + bitmapSize.width,                          // Right position (left + width)
            static_cast<float>(iY) + bitmapSize.height                          // Bottom position (top + height)
        );

        // Define the source rectangle (entire bitmap)
        D2D1_RECT_F srcRect = D2D1::RectF(
            0.0f,                                                               // Source left
            0.0f,                                                               // Source top
            bitmapSize.width,                                                   // Source right (full width)
            bitmapSize.height                                                   // Source bottom (full height)
        );

        // Draw the bitmap to the Direct2D render target
        m_d2dContext->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),                                   // The bitmap to draw
            destRect,                                                           // Destination rectangle
            1.0f,                                                               // Opacity (fully opaque)
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                              // Linear interpolation mode
            srcRect                                                             // Source rectangle
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D object %d blitted successfully. Size: %.0fx%.0f",
            static_cast<int>(iIndex), bitmapSize.width, bitmapSize.height);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Blit2DObject: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Blit 2D Object to Specific Size
//-----------------------------------------
void DX12Renderer::Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Blitting 2D object %d to size at position (%d, %d) with size (%d, %d)",
        static_cast<int>(iIndex), iX, iY, iWidth, iHeight);
#endif

    try {
        // Check if the object index is valid
        if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid 2D object index: %d", static_cast<int>(iIndex));
            return;
        }

        // Check if DirectX 11 compatibility is available for 2D rendering
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility or Direct2D context not available for 2D blitting.");
            return;
        }

        // Check if the texture exists
        if (!m_d2dTextures[int(iIndex)]) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 2D texture %d not loaded for blitting.", static_cast<int>(iIndex));
            return;
        }

        // Get the size of the bitmap
        D2D1_SIZE_F bitmapSize = m_d2dTextures[int(iIndex)]->GetSize();

        // Define the destination rectangle with specified size
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iX),                                             // Left position
            static_cast<float>(iY),                                             // Top position
            static_cast<float>(iX + iWidth),                                    // Right position (left + specified width)
            static_cast<float>(iY + iHeight)                                    // Bottom position (top + specified height)
        );

        // Define the source rectangle (entire bitmap)
        D2D1_RECT_F srcRect = D2D1::RectF(
            0.0f,                                                               // Source left
            0.0f,                                                               // Source top
            bitmapSize.width,                                                   // Source right (full width)
            bitmapSize.height                                                   // Source bottom (full height)
        );

        // Draw the bitmap to the Direct2D render target with scaling
        m_d2dContext->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),                                   // The bitmap to draw
            destRect,                                                           // Destination rectangle (scaled)
            1.0f,                                                               // Opacity (fully opaque)
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                              // Linear interpolation for scaling
            srcRect                                                             // Source rectangle (full bitmap)
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D object %d blitted to size successfully. Original: %.0fx%.0f, Target: %dx%d",
            static_cast<int>(iIndex), bitmapSize.width, bitmapSize.height, iWidth, iHeight);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Blit2DObjectToSize: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Blit 2D Object at Offset (for sprite sheets)
//-----------------------------------------
void DX12Renderer::Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Blitting 2D object %d at offset. Pos: (%d, %d), Offset: (%d, %d), Tile: (%d, %d)",
        static_cast<int>(iIndex), iBlitX, iBlitY, iXOffset, iYOffset, iTileSizeX, iTileSizeY);
#endif

    try {
        // Check if the object index is valid
        if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid 2D object index: %d", static_cast<int>(iIndex));
            return;
        }

        // Check if DirectX 11 compatibility is available for 2D rendering
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility or Direct2D context not available for 2D blitting.");
            return;
        }

        // Check if the texture exists
        if (!m_d2dTextures[int(iIndex)]) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 2D texture %d not loaded for blitting.", static_cast<int>(iIndex));
            return;
        }

        // Define the destination rectangle where the tile will be drawn
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iBlitX),                                         // Left position
            static_cast<float>(iBlitY),                                         // Top position
            static_cast<float>(iBlitX + iTileSizeX),                            // Right position (left + tile width)
            static_cast<float>(iBlitY + iTileSizeY)                             // Bottom position (top + tile height)
        );

        // Define the source rectangle using offset and tile size
        D2D1_RECT_F srcRect = D2D1::RectF(
            static_cast<float>(iXOffset),                                       // Source left (offset X)
            static_cast<float>(iYOffset),                                       // Source top (offset Y)
            static_cast<float>(iXOffset + iTileSizeX),                          // Source right (offset X + tile width)
            static_cast<float>(iYOffset + iTileSizeY)                           // Source bottom (offset Y + tile height)
        );

        // Draw the bitmap tile to the Direct2D render target
        m_d2dContext->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),                                   // The bitmap to draw
            destRect,                                                           // Destination rectangle
            1.0f,                                                               // Opacity (fully opaque)
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                              // Linear interpolation mode
            srcRect                                                             // Source rectangle (specific tile)
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D object %d tile blitted successfully at offset.", static_cast<int>(iIndex));
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Blit2DObjectAtOffset: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Blit 2D Wrapped Object at Offset (for tiled scrolling)
//-----------------------------------------
void DX12Renderer::Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Blitting 2D wrapped object %d. Pos: (%d, %d), Offset: (%d, %d), Tile: (%d, %d)",
        static_cast<int>(iIndex), iBlitX, iBlitY, iXOffset, iYOffset, iTileSizeX, iTileSizeY);
#endif

    try {
        // Check if the object index is valid
        if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid 2D object index: %d", static_cast<int>(iIndex));
            return;
        }

        // Check if DirectX 11 compatibility is available for 2D rendering
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility or Direct2D context not available for 2D blitting.");
            return;
        }

        // Check if the texture exists
        if (!m_d2dTextures[int(iIndex)]) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 2D texture %d not loaded for blitting.", static_cast<int>(iIndex));
            return;
        }

        // Get bitmap dimensions
        ID2D1Bitmap* bitmap = m_d2dTextures[int(iIndex)].Get();
        D2D1_SIZE_F bmpSize = bitmap->GetSize();
        int bmpW = static_cast<int>(bmpSize.width);
        int bmpH = static_cast<int>(bmpSize.height);

        if (bmpW <= 0 || bmpH <= 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid bitmap dimensions for wrapped blitting.");
            return;
        }

        // Normalize offsets to wrap within source image bounds
        iXOffset = ((iXOffset % bmpW) + bmpW) % bmpW;                           // Ensure positive wrap
        iYOffset = ((iYOffset % bmpH) + bmpH) % bmpH;                           // Ensure positive wrap

        // Calculate first tile region (from offset to edge of source)
        int srcW1 = bmpW - iXOffset;                                            // Width from offset to right edge
        int srcH1 = bmpH - iYOffset;                                            // Height from offset to bottom edge

        // Calculate corresponding destination size based on full stretch
        float scaleX = static_cast<float>(iTileSizeX) / bmpW;                   // X scale factor
        float scaleY = static_cast<float>(iTileSizeY) / bmpH;                   // Y scale factor

        int destW1 = static_cast<int>(srcW1 * scaleX);                          // Scaled width for first tile
        int destH1 = static_cast<int>(srcH1 * scaleY);                          // Scaled height for first tile

        // Part 1: Bottom-right (main part from offset)
        D2D1_RECT_F src1 = D2D1::RectF(
            static_cast<float>(iXOffset),                                       // Source left (offset)
            static_cast<float>(iYOffset),                                       // Source top (offset)
            static_cast<float>(bmpW),                                           // Source right (bitmap width)
            static_cast<float>(bmpH)                                            // Source bottom (bitmap height)
        );
        D2D1_RECT_F dest1 = D2D1::RectF(
            static_cast<float>(iBlitX),                                         // Destination left
            static_cast<float>(iBlitY),                                         // Destination top
            static_cast<float>(iBlitX + destW1),                                // Destination right
            static_cast<float>(iBlitY + destH1)                                 // Destination bottom
        );
        m_d2dContext->DrawBitmap(bitmap, dest1, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src1);

        // Part 2: Bottom-left (wrap X coordinate)
        if (destW1 < iTileSizeX)
        {
            D2D1_RECT_F src2 = D2D1::RectF(
                0,                                                              // Source left (wrapped to beginning)
                static_cast<float>(iYOffset),                                   // Source top (same Y offset)
                static_cast<float>(bmpW - srcW1),                               // Source right (remaining width)
                static_cast<float>(bmpH)                                        // Source bottom (bitmap height)
            );
            D2D1_RECT_F dest2 = D2D1::RectF(
                static_cast<float>(iBlitX + destW1),                            // Destination left (after first part)
                static_cast<float>(iBlitY),                                     // Destination top
                static_cast<float>(iBlitX + iTileSizeX),                        // Destination right (full tile width)
                static_cast<float>(iBlitY + destH1)                             // Destination bottom
            );
            m_d2dContext->DrawBitmap(bitmap, dest2, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src2);
        }

        // Part 3: Top-right (wrap Y coordinate)
        if (destH1 < iTileSizeY)
        {
            D2D1_RECT_F src3 = D2D1::RectF(
                static_cast<float>(iXOffset),                                   // Source left (same X offset)
                0,                                                              // Source top (wrapped to beginning)
                static_cast<float>(bmpW),                                       // Source right (bitmap width)
                static_cast<float>(bmpH - srcH1)                                // Source bottom (remaining height)
            );
            D2D1_RECT_F dest3 = D2D1::RectF(
                static_cast<float>(iBlitX),                                     // Destination left
                static_cast<float>(iBlitY + destH1),                            // Destination top (after first part)
                static_cast<float>(iBlitX + destW1),                            // Destination right
                static_cast<float>(iBlitY + iTileSizeY)                         // Destination bottom (full tile height)
            );
            m_d2dContext->DrawBitmap(bitmap, dest3, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src3);
        }

        // Part 4: Top-left corner (wrap both X and Y coordinates)
        if (destW1 < iTileSizeX && destH1 < iTileSizeY)
        {
            D2D1_RECT_F src4 = D2D1::RectF(
                0,                                                              // Source left (wrapped X)
                0,                                                              // Source top (wrapped Y)
                static_cast<float>(bmpW - srcW1),                               // Source right (remaining width)
                static_cast<float>(bmpH - srcH1)                                // Source bottom (remaining height)
            );
            D2D1_RECT_F dest4 = D2D1::RectF(
                static_cast<float>(iBlitX + destW1),                            // Destination left (after first part)
                static_cast<float>(iBlitY + destH1),                            // Destination top (after first part)
                static_cast<float>(iBlitX + iTileSizeX),                        // Destination right (full tile width)
                static_cast<float>(iBlitY + iTileSizeY)                         // Destination bottom (full tile height)
            );
            m_d2dContext->DrawBitmap(bitmap, dest4, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src4);
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D wrapped object %d blitted successfully with %d parts.",
            static_cast<int>(iIndex), (destW1 < iTileSizeX ? 1 : 0) + (destH1 < iTileSizeY ? 1 : 0) + 1);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Blit2DWrappedObjectAtOffset: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Draw Colored Pixel for DirectX 12
//-----------------------------------------
void DX12Renderer::Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing colored pixel at (%d, %d) with size %.2f", x, y, pixelSize);
#endif

    try {
        // Check if DirectX 11 compatibility is available for 2D rendering
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility or Direct2D context not available for pixel drawing.");
            return;
        }

        // Check for resize state
        if (threadManager.threadVars.bIsResizing.load()) return;

        // Create or reuse a solid color brush for pixel rendering
        static ComPtr<ID2D1SolidColorBrush> pBrush;
        if (!pBrush)
        {
            HRESULT hr = m_d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(color.x, color.y, color.z, color.w),               // RGBA color
                &pBrush                                                         // Output brush
            );
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create solid color brush for pixel.");
                return;
            }
        }
        else
        {
            // Update existing brush color
            pBrush->SetColor(D2D1::ColorF(color.x, color.y, color.z, color.w));
        }

        // Define pixel rectangle
        D2D1_RECT_F pixelRect = D2D1::RectF(
            static_cast<FLOAT>(x),                                              // Left
            static_cast<FLOAT>(y),                                              // Top
            static_cast<FLOAT>(x) + pixelSize,                                  // Right (left + size)
            static_cast<FLOAT>(y) + pixelSize                                   // Bottom (top + size)
        );

        // Fill the pixel rectangle
        m_d2dContext->FillRectangle(&pixelRect, pBrush.Get());

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Colored pixel drawn successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Blit2DColoredPixel: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Draw Rectangle for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing rectangle at (%.2f, %.2f) with size (%.2f, %.2f) - 2D: %s",
        position.x, position.y, size.x, size.y, is2D ? L"Yes" : L"No");
#endif

    try {
        if (is2D) {
            // Direct2D implementation using compatibility layer
            if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility not available for 2D rectangle drawing.");
                return;
            }

            // Create solid color brush for rectangle
            ComPtr<ID2D1SolidColorBrush> brush;
            DirectX::XMFLOAT4 convColor = ConvertColor(color.r, color.g, color.b, color.a);
            HRESULT hr = m_d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(convColor.x, convColor.y, convColor.z, convColor.w),
                &brush
            );
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create brush for 2D rectangle.");
                return;
            }

            // Define rectangle bounds
            D2D1_RECT_F rect = D2D1::RectF(
                position.x,                                                     // Left
                position.y,                                                     // Top
                position.x + size.x,                                            // Right
                position.y + size.y                                             // Bottom
            );

            // Fill the rectangle
            m_d2dContext->FillRectangle(&rect, brush.Get());

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D rectangle drawn successfully.");
#endif
        }
        else {
            // DirectX 12 3D implementation - would require vertex buffer creation
            // For now, we'll implement a basic version using immediate geometry

            // TODO: Implement 3D rectangle rendering using DirectX 12 command lists
            // This would require creating vertex buffers, binding them, and drawing

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 3D rectangle rendering not yet implemented.");
#endif
        }
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawRectangle: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Draw Text for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing text at (%.2f, %.2f) with font size %.2f: %s",
        position.x, position.y, FontSize, text.substr(0, 50).c_str());
#endif

    try {
        // Early exit checks
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext || !m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility, Direct2D context, or DirectWrite factory not available.");
            return;
        }

        if (text.empty() || FontSize <= 0.0f) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Invalid text or font size for text drawing.");
            return;
        }

        // Create text format
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            FontName,                                                           // Default font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format.");
            return;
        }

        // Convert MyColor (0-255) to DirectX float (0.0-1.0) for Direct2D
        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        // Create brush with normalized color values
        ComPtr<ID2D1SolidColorBrush> brush;
        hr = m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &brush);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text brush.");
            return;
        }

        // Define text destination rectangle
        D2D1_RECT_F destRect = D2D1::RectF(
            position.x,                                                         // Left
            position.y,                                                         // Top
            position.x + 1000.0f,                                               // Right (large enough for text)
            position.y + 200.0f                                                 // Bottom (large enough for text)
        );

        // Render the text with transparency support
        m_d2dContext->DrawText(
            text.c_str(),                                                       // Text to draw
            static_cast<UINT32>(text.size()),                                   // Text length
            textFormat.Get(),                                                   // Text format
            destRect,                                                           // Destination rectangle
            brush.Get()                                                         // Text brush
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Text drawn successfully. Color: RGBA(%.3f,%.3f,%.3f,%.3f)", r, g, b, a);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawMyText: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Draw Text with Size for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing sized text at (%.2f, %.2f) with size (%.2f, %.2f): %s",
        position.x, position.y, size.x, size.y, text.substr(0, 50).c_str());
#endif

    try {
        // Check availability of required components
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext || !m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Required components not available for sized text drawing.");
            return;
        }

        // Create text format
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            FontName,                                                           // Default font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format for sized text.");
            return;
        }

        // Create brush for text rendering
        ComPtr<ID2D1SolidColorBrush> brush;
        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        hr = m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &brush);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create brush for sized text.");
            return;
        }

        // Define text rectangle with specified size
        D2D1_RECT_F textRect = D2D1::RectF(
            position.x,                                                         // Left
            position.y,                                                         // Top
            position.x + size.x,                                                // Right (left + width)
            position.y + size.y                                                 // Bottom (top + height)
        );

        // Draw text within the specified rectangle
        m_d2dContext->DrawText(
            text.c_str(),                                                       // Text to draw
            static_cast<UINT32>(text.size()),                                   // Text length
            textFormat.Get(),                                                   // Text format
            textRect,                                                           // Destination rectangle
            brush.Get()                                                         // Text brush
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Sized text drawn successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawMyText (sized): %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Draw Text with Custom Font for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, const std::wstring& fontName) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing text with font '%s' at (%.2f, %.2f): %s",
        fontName.c_str(), position.x, position.y, text.substr(0, 50).c_str());
#endif

    try {
        // Early exit checks
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext || !m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Required components not available for custom font text drawing.");
            return;
        }

        if (text.empty() || FontSize <= 0.0f) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Invalid text or font size for custom font text drawing.");
            return;
        }

        // Create text format with specified font name
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            fontName.c_str(),                                                   // Custom font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format with font: %s", fontName.c_str());
            return;
        }

        // Convert MyColor to DirectX float values
        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        // Create brush with normalized color values
        ComPtr<ID2D1SolidColorBrush> brush;
        hr = m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &brush);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text brush for custom font.");
            return;
        }

        // Define destination rectangle for text
        D2D1_RECT_F destRect = D2D1::RectF(
            position.x,                                                         // Left
            position.y,                                                         // Top
            position.x + 1000.0f,                                               // Right (large enough)
            position.y + 200.0f                                                 // Bottom (large enough)
        );

        // Render text with custom font and transparency support
        m_d2dContext->DrawText(
            text.c_str(),                                                       // Text to draw
            static_cast<UINT32>(text.size()),                                   // Text length
            textFormat.Get(),                                                   // Custom text format
            destRect,                                                           // Destination rectangle
            brush.Get()                                                         // Text brush
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Custom font text drawn successfully. Font: %s, Color: RGBA(%.3f,%.3f,%.3f,%.3f)",
            fontName.c_str(), r, g, b, a);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawMyTextWithFont: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Draw Centered Text for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing centered text in control (%.2f x %.2f) at (%.2f, %.2f): %s",
        controlWidth, controlHeight, position.x, position.y, text.substr(0, 50).c_str());
#endif

    try {
        // Check availability of required components
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext || !m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Required components not available for centered text drawing.");
            return;
        }

        // Create text format with center alignment
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            FontName,                                                           // Default font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format for centered text.");
            return;
        }

        // Set text alignment to center
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Create brush for text rendering
        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        ComPtr<ID2D1SolidColorBrush> brush;
        hr = m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &brush);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create brush for centered text.");
            return;
        }

        // Define control rectangle for centering
        D2D1_RECT_F controlRect = D2D1::RectF(
            position.x,                                                         // Left
            position.y,                                                         // Top
            position.x + controlWidth,                                          // Right
            position.y + controlHeight                                          // Bottom
        );

        // Draw text centered within the control bounds
        m_d2dContext->DrawText(
            text.c_str(),                                                       // Text to draw
            static_cast<UINT32>(text.size()),                                   // Text length
            textFormat.Get(),                                                   // Text format with centering
            controlRect,                                                        // Control rectangle
            brush.Get()                                                         // Text brush
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Centered text drawn successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawMyTextCentered: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Load All Known Textures for DirectX 12
//-----------------------------------------
bool DX12Renderer::LoadAllKnownTextures()
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Loading all known textures...");
#endif

    try {
        // Load all required 2D textures using DirectX 11 compatibility layer
        bool result = true;
        int texturesLoaded = 0;
        int texturesFailed = 0;

        for (int i = 0; i < MAX_TEXTURE_BUFFERS; i++)
        {
            auto fileName = AssetsDir / texFilename[i];

            // Load the texture using compatibility layer
            if (!LoadTexture(i, fileName, true))
            {
                std::wstring msg = L"DX12Renderer: Failed to load 2D Texture: " + fileName.wstring();
                debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
                texturesFailed++;
                result = false;
            }
            else
            {
                texturesLoaded++;
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                std::wstring msg = L"DX12Renderer: Successfully loaded 2D Texture: " + fileName.wstring();
                debug.logLevelMessage(LogLevel::LOG_DEBUG, msg);
#endif
            }
        }

        // Load 3D textures (DDS format) for DirectX 12
        // Implementation will be added in Step 7

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture loading completed. Loaded: %d, Failed: %d", texturesLoaded, texturesFailed);
#endif

        return result;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in LoadAllKnownTextures: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Clean 2D Textures for DirectX 12
//-----------------------------------------
void DX12Renderer::Clean2DTextures()
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Cleaning 2D textures...");
#endif

    try {
        for (int i = 0; i < MAX_TEXTURE_BUFFERS; ++i)
        {
            if (m_d2dTextures[i])
            {
                m_d2dTextures[i].Reset();
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D Texture [%d] released.", i);
#endif
            }
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: All 2D textures cleaned successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Clean2DTextures: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Clear 2D Blit Queue for DirectX 12
//-----------------------------------------
void DX12Renderer::Clear2DBlitQueue()
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Clearing 2D blit queue...");
#endif

    try {
        for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
        {
            SecureZeroMemory(&My2DBlitQueue[iX], sizeof(GFXObjQueue));
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D blit queue cleared successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Clear2DBlitQueue: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Place 2D Blit Object to Queue for DirectX 12
//-----------------------------------------
bool DX12Renderer::Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl, BlitObj2DType objType, BlitObj2DDetails objDetails, CanBlitType BlitType)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Adding 2D blit object to queue. Index: %d, Type: %d", static_cast<int>(iIndex), static_cast<int>(objType));
#endif

    try {
        //////////////////////////////////////////////////
        // Check if the object is already in the queue
        //////////////////////////////////////////////////
        for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
        {
            // We need to find single objects only and check if already in the queue
            switch (BlitType)
            {
                // Is the Queue Object of the following:-
            case CanBlitType::CAN_BLIT_SINGLE:
                if (My2DBlitQueue[iX].bInUse)
                {
                    if (My2DBlitQueue[iX].BlitObjDetails.iBlitID == iIndex)
                    {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D blit object already in queue, skipping.");
#endif
                        return FALSE;
                    }
                }
                break;
            } // End of switch (BlitType)
        } // End of for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)

        //////////////////////////////////////////////////
        // Find an empty slot in the queue
        //////////////////////////////////////////////////
        for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
        {
            if (!My2DBlitQueue[iX].bInUse)
            {
                My2DBlitQueue[iX].bInUse = TRUE;
                My2DBlitQueue[iX].BlitPhase = BlitPhaseLvl;
                My2DBlitQueue[iX].BlitObjType = objType;
                My2DBlitQueue[iX].BlitObjDetails = objDetails;
                My2DBlitQueue[iX].BlitObjDetails.iBlitID = iIndex;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D blit object added to queue at slot %d.", iX);
#endif
                return TRUE;
            }
        }

        // No empty slots found
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: No empty slots in 2D blit queue.");
#endif
        return FALSE;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Place2DBlitObjectToQueue: %s", errorMsg.c_str());
        return FALSE;
    }
}

//-----------------------------------------
// Draw Texture for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawTexture(int textureIndex, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing texture %d at (%.2f, %.2f) with size (%.2f, %.2f) - 2D: %s",
        textureIndex, position.x, position.y, size.x, size.y, is2D ? L"Yes" : L"No");
#endif

    try {
        if (is2D) {
            // DirectX 11 compatibility layer implementation for 2D textures
            if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS || !m_d2dTextures[textureIndex]) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Invalid 2D texture index or texture not loaded: %d", textureIndex);
                return;
            }

            if (!IsDX11CompatibilityAvailable() || !m_d2dContext) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectX 11 compatibility not available for 2D texture drawing.");
                return;
            }

            // Convert tint color to Direct2D color format
            float tintR = static_cast<float>(tintColor.r) / 255.0f;
            float tintG = static_cast<float>(tintColor.g) / 255.0f;
            float tintB = static_cast<float>(tintColor.b) / 255.0f;
            float tintA = static_cast<float>(tintColor.a) / 255.0f;

            // Define destination rectangle
            D2D1_RECT_F destRect = D2D1::RectF(
                position.x,                                                     // Left
                position.y,                                                     // Top
                position.x + size.x,                                            // Right
                position.y + size.y                                             // Bottom
            );

            // Create color matrix effect for tinting if needed
            if (tintColor.r != 255 || tintColor.g != 255 || tintColor.b != 255 || tintColor.a != 255) {
                // For complex tinting, we would use Direct2D effects
                // For now, we'll use simple opacity and blend modes
                m_d2dContext->DrawBitmap(
                    m_d2dTextures[textureIndex].Get(),                          // Source bitmap
                    destRect,                                                   // Destination rectangle
                    tintA,                                                      // Alpha/opacity from tint color
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                      // Linear interpolation
                    nullptr                                                     // Source rectangle (entire bitmap)
                );
            }
            else {
                // No tinting needed, draw normally
                m_d2dContext->DrawBitmap(
                    m_d2dTextures[textureIndex].Get(),                          // Source bitmap
                    destRect,                                                   // Destination rectangle
                    1.0f,                                                       // Full opacity
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                      // Linear interpolation
                    nullptr                                                     // Source rectangle (entire bitmap)
                );
            }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D texture %d drawn successfully with tint RGBA(%.3f,%.3f,%.3f,%.3f)",
                textureIndex, tintR, tintG, tintB, tintA);
#endif
        }
        else {
            // DirectX 12 native implementation for 3D textures
            if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D || !m_d3d12Textures[textureIndex]) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Invalid 3D texture index or texture not loaded: %d", textureIndex);
                return;
            }

            // TODO: Implement 3D texture rendering using DirectX 12 command lists
            // This would require creating vertex buffers for a textured quad and binding the texture

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 3D texture rendering not yet fully implemented.");
#endif
        }
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawTexture: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Get Character Width for DirectX 12
//-----------------------------------------
float DX12Renderer::GetCharacterWidth(wchar_t character, float FontSize) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Getting character width for '%c' with font size %.2f", character, FontSize);
#endif

    try {
        // Check if DirectWrite factory is available
        if (!m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectWrite factory not initialized for character width calculation.");
            return 0.0f;
        }

        // Create text format for character measurement
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            FontName,                                                           // Default font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format for character width calculation.");
            return 0.0f;
        }

        // Create text layout for the single character
        ComPtr<IDWriteTextLayout> textLayout;
        hr = m_dwriteFactory->CreateTextLayout(
            &character,                                                         // Character to measure
            1,                                                                  // Length (single character)
            textFormat.Get(),                                                   // Text format
            1000.0f,                                                            // Maximum width
            1000.0f,                                                            // Maximum height
            &textLayout                                                         // Output text layout
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text layout for character width calculation.");
            return 0.0f;
        }

        // Get the metrics of the text layout
        DWRITE_TEXT_METRICS textMetrics;
        hr = textLayout->GetMetrics(&textMetrics);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get text metrics for character width calculation.");
            return 0.0f;
        }

        // Return the width of the character
        float charWidth = textMetrics.width;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Character '%c' width: %.2f", character, charWidth);
#endif

        return charWidth;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in GetCharacterWidth: %s", errorMsg.c_str());
        return 0.0f;
    }
}

//-----------------------------------------
// Get Character Width with Custom Font for DirectX 12
//-----------------------------------------
float DX12Renderer::GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& fontName) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Getting character width for '%c' with font '%s' size %.2f",
        character, fontName.c_str(), FontSize);
#endif

    try {
        // Check if DirectWrite factory is available
        if (!m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectWrite factory not initialized for custom font character width calculation.");
            return 0.0f;
        }

        // Create text format with specified font name
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            fontName.c_str(),                                                   // Custom font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format for custom font character width calculation. Font: %s",
                fontName.c_str());
            return 0.0f;
        }

        // Create text layout for the single character
        ComPtr<IDWriteTextLayout> textLayout;
        hr = m_dwriteFactory->CreateTextLayout(
            &character,                                                         // Character to measure
            1,                                                                  // Length (single character)
            textFormat.Get(),                                                   // Text format with custom font
            1000.0f,                                                            // Maximum width
            1000.0f,                                                            // Maximum height
            &textLayout                                                         // Output text layout
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text layout for custom font character width calculation.");
            return 0.0f;
        }

        // Get the metrics of the text layout
        DWRITE_TEXT_METRICS textMetrics;
        hr = textLayout->GetMetrics(&textMetrics);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get text metrics for custom font character width calculation.");
            return 0.0f;
        }

        // Return the width of the character using the specified font
        float charWidth = textMetrics.width;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Character '%c' width with font '%s': %.2f",
            character, fontName.c_str(), charWidth);
#endif

        return charWidth;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in GetCharacterWidth (custom font): %s", errorMsg.c_str());
        return 0.0f;
    }
}

//-----------------------------------------
// Calculate Text Width for DirectX 12
//-----------------------------------------
float DX12Renderer::CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Calculating text width for: %s (Font size: %.2f, Container: %.2f)",
        text.substr(0, 50).c_str(), FontSize, containerWidth);
#endif

    try {
        // Check if DirectWrite factory is initialized
        if (!m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectWrite factory not initialized for text width calculation.");
            return 0.0f;
        }

        // Create text format with the specified font size
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            FontName,                                                           // Default font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format for text width calculation.");
            return 0.0f;
        }

        // Create text layout for measuring the text dimensions
        ComPtr<IDWriteTextLayout> textLayout;
        hr = m_dwriteFactory->CreateTextLayout(
            text.c_str(),                                                       // Text to measure
            static_cast<UINT32>(text.length()),                                 // Text length
            textFormat.Get(),                                                   // Text format
            containerWidth,                                                     // Maximum width (container width)
            1000.0f,                                                            // Maximum height
            &textLayout                                                         // Output text layout
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text layout for text width calculation.");
            return 0.0f;
        }

        // Get the metrics of the text layout
        DWRITE_TEXT_METRICS textMetrics;
        hr = textLayout->GetMetrics(&textMetrics);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get text metrics for text width calculation.");
            return 0.0f;
        }

        // Calculate the X position that would center the text in the container
        float centerX = (containerWidth - textMetrics.width) / 2.0f;
        float resultWidth = (centerX < 0.0f) ? 0.0f : centerX;                 // Ensure non-negative

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Text width calculation - Actual: %.2f, Centered X: %.2f",
            textMetrics.width, resultWidth);
#endif

        return resultWidth;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CalculateTextWidth: %s", errorMsg.c_str());
        return 0.0f;
    }
}

//-----------------------------------------
// Calculate Text Height for DirectX 12
//-----------------------------------------
float DX12Renderer::CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Calculating text height for: %s (Font size: %.2f, Container: %.2f)",
        text.substr(0, 50).c_str(), FontSize, containerHeight);
#endif

    try {
        // Check if DirectWrite factory is initialized
        if (!m_dwriteFactory) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DirectWrite factory not initialized for text height calculation.");
            return 0.0f;
        }

        // Create text format for text measurement
        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            FontName,                                                           // Default font name
            nullptr,                                                            // No font collection
            DWRITE_FONT_WEIGHT_NORMAL,                                          // Normal font weight
            DWRITE_FONT_STYLE_NORMAL,                                           // Normal font style
            DWRITE_FONT_STRETCH_NORMAL,                                         // Normal font stretch
            FontSize,                                                           // Font size
            L"en-us",                                                           // Locale
            &textFormat                                                         // Output text format
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text format for text height calculation.");
            return 0.0f;
        }

        // Create text layout for the text string
        ComPtr<IDWriteTextLayout> textLayout;
        hr = m_dwriteFactory->CreateTextLayout(
            text.c_str(),                                                       // Text to measure
            static_cast<UINT32>(text.length()),                                 // Text length
            textFormat.Get(),                                                   // Text format
            1000.0f,                                                            // Maximum width
            containerHeight,                                                    // Maximum height (container height)
            &textLayout                                                         // Output text layout
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text layout for text height calculation.");
            return 0.0f;
        }

        // Get the metrics of the text layout
        DWRITE_TEXT_METRICS textMetrics;
        hr = textLayout->GetMetrics(&textMetrics);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get text metrics for text height calculation.");
            return 0.0f;
        }

        // Return the height of the text
        float textHeight = textMetrics.height;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Text height calculated: %.2f", textHeight);
#endif

        return textHeight;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CalculateTextHeight: %s", errorMsg.c_str());
        return 0.0f;
    }
}

//-----------------------------------------
// Draw Video Frame for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawVideoFrame(const Vector2& position, const Vector2& size, const MyColor& tintColor, ComPtr<ID3D12Resource> videoTexture) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Drawing video frame at (%.2f, %.2f) with size (%.2f, %.2f)",
        position.x, position.y, size.x, size.y);
#endif

    try {
        // Early exit if no valid texture or required components
        if (!videoTexture || !IsDX11CompatibilityAvailable() || !m_d2dContext) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Missing required resources for video frame rendering.");
            return;
        }

        // We're already in the renderer mutex scope from the caller
        // No need to acquire it again to avoid deadlocks

        // Get the texture description to determine dimensions
        D3D12_RESOURCE_DESC textureDesc = videoTexture->GetDesc();

        // For DirectX 12 video frame rendering, we need to:
        // 1. Create a staging texture for CPU access
        // 2. Copy the video texture to the staging texture
        // 3. Map the staging texture to get pixel data
        // 4. Create a Direct2D bitmap from the pixel data
        // 5. Render the bitmap using Direct2D

        // Create a staging texture for CPU access
        D3D12_RESOURCE_DESC stagingDesc = textureDesc;
        stagingDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;             // Ensure 2D texture
        stagingDesc.Alignment = 0;                                              // Default alignment
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;                       // BGRA format for Direct2D compatibility
        stagingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;                    // Row-major layout for CPU access
        stagingDesc.Flags = D3D12_RESOURCE_FLAG_NONE;                           // No special flags

        ComPtr<ID3D12Resource> stagingTexture;
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_READBACK);
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &uploadHeapProps,                                                   // Readback heap for CPU access
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &stagingDesc,                                                       // Staging texture description
            D3D12_RESOURCE_STATE_COPY_DEST,                                     // Initial state for copying
            nullptr,                                                            // No optimized clear value
            IID_PPV_ARGS(&stagingTexture)                                       // Output staging texture
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create staging texture for video frame.");
            return;
        }

        // Copy the video texture to the staging texture using a separate command list
        // Note: This should ideally be done on a copy queue for better performance
        ComPtr<ID3D12CommandAllocator> copyAllocator;
        hr = m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copyAllocator));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create copy command allocator for video frame.");
            return;
        }

        ComPtr<ID3D12GraphicsCommandList> copyCommandList;
        hr = m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copyAllocator.Get(), nullptr, IID_PPV_ARGS(&copyCommandList));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create copy command list for video frame.");
            return;
        }

        // Transition video texture to copy source state
        CD3DX12_RESOURCE_BARRIER copySourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            videoTexture.Get(),
            D3D12_RESOURCE_STATE_COMMON,                                        // Assume common state
            D3D12_RESOURCE_STATE_COPY_SOURCE
        );
        copyCommandList->ResourceBarrier(1, &copySourceBarrier);

        // Copy texture data
        copyCommandList->CopyResource(stagingTexture.Get(), videoTexture.Get());

        // Transition video texture back to common state
        CD3DX12_RESOURCE_BARRIER copyDestBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            videoTexture.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_COMMON
        );
        copyCommandList->ResourceBarrier(1, &copyDestBarrier);

        // Close and execute the copy command list
        hr = copyCommandList->Close();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to close copy command list for video frame.");
            return;
        }

        ID3D12CommandList* copyLists[] = { copyCommandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, copyLists);

        // Wait for copy operation to complete
        WaitForGPUToFinish();

        // Map the staging texture to get pixel data
        void* mappedData = nullptr;
        D3D12_RANGE readRange = { 0, 0 };                                       // Read entire resource
        hr = stagingTexture->Map(0, &readRange, &mappedData);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to map staging texture for video frame.");
            return;
        }

        // Calculate texture properties
        UINT textureWidth = static_cast<UINT>(textureDesc.Width);
        UINT textureHeight = static_cast<UINT>(textureDesc.Height);
        UINT bytesPerPixel = 4;                                                 // BGRA format
        UINT rowPitch = textureWidth * bytesPerPixel;

        // Create Direct2D bitmap from the pixel data
        D2D1_SIZE_U bitmapSize = D2D1::SizeU(textureWidth, textureHeight);
        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        ComPtr<ID2D1Bitmap> d2dBitmap;
        hr = m_d2dContext->CreateBitmap(
            bitmapSize,                                                         // Bitmap size
            mappedData,                                                         // Pixel data
            rowPitch,                                                           // Row pitch
            bitmapProps,                                                        // Bitmap properties
            &d2dBitmap                                                          // Output bitmap
        );

        // Unmap the staging texture
        stagingTexture->Unmap(0, nullptr);

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create Direct2D bitmap from video frame data.");
            return;
        }

        // Draw the bitmap using Direct2D
        D2D1_RECT_F destRect = D2D1::RectF(
            position.x,                                                         // Left
            position.y,                                                         // Top
            position.x + size.x,                                                // Right
            position.y + size.y                                                 // Bottom
        );

        // Apply tint color through opacity
        float opacity = static_cast<float>(tintColor.a) / 255.0f;

        // Draw the video frame bitmap
        m_d2dContext->DrawBitmap(
            d2dBitmap.Get(),                                                    // Video frame bitmap
            destRect,                                                           // Destination rectangle
            opacity,                                                            // Opacity from tint color
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR                               // Linear interpolation for scaling
        );

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Video frame rendered successfully. Size: %dx%d, Opacity: %.3f",
            textureWidth, textureHeight, opacity);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in DrawVideoFrame: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Resize DirectX 12 Renderer
//-----------------------------------------
void DX12Renderer::Resize(uint32_t width, uint32_t height)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Resize requested: %dx%d", width, height);
#endif

    std::string lockName = "dx12_renderer_resize_lock";

    // Try to acquire the render mutex with a 1000ms timeout
    if (!threadManager.TryLock(lockName, 1000)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Could not acquire render mutex for resize operation - timeout reached");
        return;
    }

    try {
        if (!m_swapChain || !m_d3d12Device || !m_commandQueue)
        {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Missing critical DirectX 12 interfaces for resize.");
            threadManager.RemoveLock(lockName);
            return;
        }

        // Save old window size (only in windowed mode)
        BOOL isFullscreen = FALSE;
        m_swapChain->GetFullscreenState(&isFullscreen, nullptr);
        if (!isFullscreen)
        {
            prevWindowedWidth = iOrigWidth;
            prevWindowedHeight = iOrigHeight;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Saved previous windowed size: %dx%d", prevWindowedWidth, prevWindowedHeight);
#endif
        }

        // Wait for GPU to finish all operations before resize
        WaitForGPUToFinish();

        // Clean up DirectX 11-12 compatibility layer before resize
        if (m_d2dContext) {
            m_d2dContext->SetTarget(nullptr);                                   // Clear Direct2D render target
            m_d2dContext->Flush();                                              // Flush pending Direct2D operations
            D2DBusy.store(false);                                               // Mark Direct2D as not busy
        }

        // Release all render target views and related resources
        for (UINT i = 0; i < FrameCount; ++i) {
            if (m_frameContexts[i].renderTarget) {
                m_frameContexts[i].renderTarget.Reset();
            }
        }

        // Clean up 2D textures that may reference old render targets
        Clean2DTextures();

        // Release depth stencil buffer
        if (m_depthStencilBuffer) {
            m_depthStencilBuffer.Reset();
        }

        // Resize swap chain buffers
        HRESULT hr = m_swapChain->ResizeBuffers(
            FrameCount,                                                         // Buffer count
            width,                                                              // New width
            height,                                                             // New height
            DXGI_FORMAT_R8G8B8A8_UNORM,                                         // Format
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH                              // Allow mode switching
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to resize swap chain buffers.");
            threadManager.RemoveLock(lockName);
            return;
        }

        // Recreate render target views for each frame
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.cpuStart);
        for (UINT i = 0; i < FrameCount; ++i) {
            // Get the back buffer resource from the swap chain
            hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_frameContexts[i].renderTarget));
            if (FAILED(hr)) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get swap chain buffer %d after resize.", i);
                threadManager.RemoveLock(lockName);
                return;
            }

            // Create render target view for this buffer
            m_d3d12Device->CreateRenderTargetView(m_frameContexts[i].renderTarget.Get(), nullptr, rtvHandle);

            // Store the RTV handle for this frame
            m_frameContexts[i].rtvHandle = rtvHandle;

            // Set buffer name for debugging
            std::wstring bufferName = L"DX12Renderer_BackBuffer_" + std::to_wstring(i) + L"_Resized";
            m_frameContexts[i].renderTarget->SetName(bufferName.c_str());

            // Move to the next descriptor handle
            rtvHandle.Offset(1, m_rtvHeap.handleIncrementSize);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Recreated RTV for frame %d after resize.", i);
#endif
        }

        // Recreate depth stencil buffer with new dimensions
        D3D12_RESOURCE_DESC depthStencilDesc = {};
        depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;        // 2D texture
        depthStencilDesc.Alignment = 0;                                         // Default alignment
        depthStencilDesc.Width = width;                                         // New width
        depthStencilDesc.Height = height;                                       // New height
        depthStencilDesc.DepthOrArraySize = 1;                                  // Single depth slice
        depthStencilDesc.MipLevels = 1;                                         // Single mip level
        depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                // 24-bit depth, 8-bit stencil
        depthStencilDesc.SampleDesc.Count = 1;                                  // No multisampling
        depthStencilDesc.SampleDesc.Quality = 0;                                // No multisampling quality
        depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;                 // Driver-optimized layout
        depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;       // Allow depth stencil usage

        // Define the clear value for optimal performance
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                      // Match depth buffer format
        clearValue.DepthStencil.Depth = 1.0f;                                   // Clear to maximum depth
        clearValue.DepthStencil.Stencil = 0;                                    // Clear stencil to zero

        // Create the new depth stencil buffer
        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        hr = m_d3d12Device->CreateCommittedResource(
            &defaultHeapProps,                                                  // Default heap for GPU access
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &depthStencilDesc,                                                  // Resource description
            D3D12_RESOURCE_STATE_DEPTH_WRITE,                                   // Initial state for depth writing
            &clearValue,                                                        // Optimal clear value
            IID_PPV_ARGS(&m_depthStencilBuffer)                                 // Output depth stencil buffer
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create depth stencil buffer after resize.");
            threadManager.RemoveLock(lockName);
            return;
        }

        // Set depth buffer name for debugging
        m_depthStencilBuffer->SetName(L"DX12Renderer_DepthStencilBuffer_Resized");

        // Create new depth stencil view
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                         // Match buffer format
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;                  // 2D texture view
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;                                    // No special flags
        dsvDesc.Texture2D.MipSlice = 0;                                         // Use mip level 0

        // Create the depth stencil view
        m_d3d12Device->CreateDepthStencilView(
            m_depthStencilBuffer.Get(),                                         // Depth stencil resource
            &dsvDesc,                                                           // DSV description
            m_dsvHeap.cpuStart                                                  // DSV heap handle
        );

        // Update frame index
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Update internal dimensions
        iOrigWidth = width;
        iOrigHeight = height;

        // Recreate DirectX 11-12 compatibility layer for 2D rendering
        CleanupDX11On12Compatibility();
        bool compatibilitySuccess = InitializeDX11On12Compatibility();
        if (!compatibilitySuccess) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to reinitialize DirectX 11-12 compatibility after resize.");
        }

        // Update camera with new dimensions
        myCamera.SetupDefaultCamera(iOrigWidth, iOrigHeight);

        // Clear resizing flag
        threadManager.threadVars.bIsResizing.store(false);

        // Release the render mutex
        threadManager.RemoveLock(lockName);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Resize completed successfully to %dx%d", width, height);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Resize: %s", errorMsg.c_str());

        // Ensure lock is released on exception
        threadManager.RemoveLock(lockName);
    }
}

//-----------------------------------------
// Set Full Screen Mode for DirectX 12
//-----------------------------------------
bool DX12Renderer::SetFullScreen(void)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: SetFullScreen() called - beginning fullscreen transition");
#endif

    // Check if already in fullscreen transition to prevent race conditions
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Fullscreen transition already in progress");
        return false;
    }

    // Set fullscreen transition flag to block other operations
    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    try {
        std::lock_guard<std::mutex> lock(s_renderMutex);                        // Ensure thread safety during fullscreen transition

        // Stop all FX effects before fullscreen transition
        fxManager.StopAllFXForResize();

        // Save current window size before going fullscreen
        RECT rc;
        GetClientRect(hwnd, &rc);
        prevWindowedWidth = rc.right - rc.left;
        prevWindowedHeight = rc.bottom - rc.top;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Saved windowed size: %dx%d", prevWindowedWidth, prevWindowedHeight);
#endif

        // Get the output (monitor) information
        ComPtr<IDXGIOutput> output;
        HRESULT hr = m_swapChain->GetContainingOutput(&output);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get containing output for swap chain");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        DXGI_OUTPUT_DESC outputDesc;
        hr = output->GetDesc(&outputDesc);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get output description");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Calculate new width and height from the monitor
        UINT fullscreenWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        UINT fullscreenHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Target fullscreen resolution: %dx%d", fullscreenWidth, fullscreenHeight);
#endif

        // Wait for GPU to complete before mode change
        WaitForGPUToFinish();

        // Set the fullscreen state
        hr = m_swapChain->SetFullscreenState(TRUE, nullptr);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set fullscreen state");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Resize to fullscreen dimensions
        Resize(fullscreenWidth, fullscreenHeight);

        // Clear all flags
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Fullscreen mode set successfully");
        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Exception in SetFullScreen: %s", errorMsg.c_str());

        // Clear all flags on exception
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);
        return false;
    }
}

//-----------------------------------------
// Set Exclusive Full Screen Mode for DirectX 12
//-----------------------------------------
bool DX12Renderer::SetFullExclusive(uint32_t width, uint32_t height)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: SetFullExclusive(%d, %d) called - beginning exclusive fullscreen transition", width, height);
#endif

    // Check if already in fullscreen transition to prevent race conditions
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Fullscreen transition already in progress");
        return false;
    }

    // Set fullscreen transition flag to block other operations
    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    try {
        std::lock_guard<std::mutex> lock(s_renderMutex);                        // Ensure thread safety during exclusive fullscreen transition

        // Stop all FX effects before exclusive fullscreen transition
        fxManager.StopAllFXForResize();

        // Save current window size before going to exclusive fullscreen
        RECT rc;
        GetClientRect(hwnd, &rc);
        prevWindowedWidth = rc.right - rc.left;
        prevWindowedHeight = rc.bottom - rc.top;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Saved windowed size: %dx%d", prevWindowedWidth, prevWindowedHeight);
#endif

        // Get the output (monitor) information for mode validation
        ComPtr<IDXGIOutput> output;
        HRESULT hr = m_swapChain->GetContainingOutput(&output);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get containing output for swap chain");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Enumerate available display modes to verify the requested resolution is supported
        UINT numModes = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

        // Get the number of available display modes
        hr = output->GetDisplayModeList(format, 0, &numModes, nullptr);
        if (FAILED(hr) || numModes == 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to enumerate display modes");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Get the actual display mode list
        std::vector<DXGI_MODE_DESC> displayModes(numModes);
        hr = output->GetDisplayModeList(format, 0, &numModes, displayModes.data());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get display mode list");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Find the closest matching display mode for the requested resolution
        DXGI_MODE_DESC targetMode = {};
        targetMode.Width = width;
        targetMode.Height = height;
        targetMode.Format = format;
        targetMode.RefreshRate.Numerator = 60;                                  // Default to 60Hz
        targetMode.RefreshRate.Denominator = 1;

        DXGI_MODE_DESC closestMode = {};
        hr = output->FindClosestMatchingMode(&targetMode, &closestMode, m_d3d12Device.Get());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to find closest matching display mode");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Closest matching mode: %dx%d @%dHz",
            closestMode.Width, closestMode.Height,
            closestMode.RefreshRate.Numerator / closestMode.RefreshRate.Denominator);
#endif

        // Wait for GPU to complete before mode change
        WaitForGPUToFinish();

        // Set to exclusive fullscreen mode with the closest matching mode
        hr = m_swapChain->SetFullscreenState(TRUE, output.Get());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set exclusive fullscreen state");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Resize to the closest matching resolution
        Resize(closestMode.Width, closestMode.Height);

        // Clear all transition flags
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Exclusive fullscreen mode set successfully at %dx%d", closestMode.Width, closestMode.Height);
#endif

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Exception in SetFullExclusive: %s", errorMsg.c_str());

        // Clear all flags on exception
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);
        return false;
    }
}

//-----------------------------------------
// Set Windowed Screen Mode for DirectX 12
//-----------------------------------------
bool DX12Renderer::SetWindowedScreen(void)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: SetWindowedScreen() called - beginning windowed transition");
#endif

    // Check if already in fullscreen transition to prevent race conditions
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Fullscreen transition already in progress");
        return false;
    }

    // Set fullscreen transition flag to block other operations
    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    try {
        std::lock_guard<std::mutex> lock(s_renderMutex);                        // Ensure thread safety during windowed transition

        // Wait for GPU to complete before mode change
        WaitForGPUToFinish();

        // Set to windowed mode
        HRESULT hr = m_swapChain->SetFullscreenState(FALSE, nullptr);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set windowed state");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // If we are shutting down, we do not need to worry about resizing buffers
        if (threadManager.threadVars.bIsShuttingDown.load()) {
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return true;
        }

        // Get appropriate window dimensions from stored values
        UINT windowedWidth = prevWindowedWidth > 0 ? prevWindowedWidth : DEFAULT_WINDOW_WIDTH;
        UINT windowedHeight = prevWindowedHeight > 0 ? prevWindowedHeight : DEFAULT_WINDOW_HEIGHT;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Target windowed resolution: %dx%d", windowedWidth, windowedHeight);
#endif

        // Resize to windowed dimensions
        Resize(windowedWidth, windowedHeight);

        // Reset window size and position to center it on screen
        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        int centerX = (workArea.right - workArea.left - windowedWidth) / 2;
        int centerY = (workArea.bottom - workArea.top - windowedHeight) / 2;

        SetWindowPos(hwnd, nullptr, centerX, centerY, windowedWidth, windowedHeight, SWP_NOZORDER);

        // Clear all flags
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Windowed mode set successfully");
        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Exception in SetWindowedScreen: %s", errorMsg.c_str());

        // Clear all flags on exception
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);
        return false;
    }
}

//-----------------------------------------
// Create Texture Resources for DirectX 12
//-----------------------------------------
void DX12Renderer::CreateTextureResources() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating texture resources and descriptors...");
#endif

    try {
        // Initialize texture descriptor allocation tracking
        UINT currentDescriptorOffset = 0;

        // Reserve space for constant buffers in the CBV/SRV/UAV heap
        currentDescriptorOffset += 10;  // Reserve first 10 slots for constant buffers

        // Create Shader Resource Views (SRVs) for loaded 3D textures
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; ++i) {
            if (m_d3d12Textures[i]) {
                // Get texture description for SRV creation
                D3D12_RESOURCE_DESC textureDesc = m_d3d12Textures[i]->GetDesc();

                // Create SRV description
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = textureDesc.Format;                             // Use texture format
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;           // 2D texture view
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // Default component mapping
                srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;             // Use all mip levels
                srvDesc.Texture2D.MostDetailedMip = 0;                           // Start from most detailed mip
                srvDesc.Texture2D.PlaneSlice = 0;                                // Default plane slice
                srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;                    // No LOD clamping

                // Calculate descriptor handle for this texture
                CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
                    m_cbvSrvUavHeap.cpuStart,
                    currentDescriptorOffset + i,
                    m_cbvSrvUavHeap.handleIncrementSize
                );

                // Create the SRV
                m_d3d12Device->CreateShaderResourceView(
                    m_d3d12Textures[i].Get(),                                    // Texture resource
                    &srvDesc,                                                    // SRV description
                    srvHandle                                                    // Descriptor handle
                );

                // Set debug name for the SRV
                std::wstring srvName = L"DX12Renderer_TextureSRV_" + std::to_wstring(i);
                // Note: SRVs don't have SetName method, but we can track them internally

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Created SRV for 3D texture %d. Format: %d, Mips: %d",
                    i, textureDesc.Format, textureDesc.MipLevels);
#endif
            }
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture resources and descriptors created successfully.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateTextureResources: %s", errorMsg.c_str());
        throw;
    }
}

//-----------------------------------------
// Upload Texture Data to GPU for DirectX 12
//-----------------------------------------
bool DX12Renderer::UploadTextureData(int textureIndex, const void* textureData, size_t dataSize, UINT width, UINT height, DXGI_FORMAT format) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Uploading texture data for texture %d. Size: %zu bytes, Dimensions: %dx%d",
        textureIndex, dataSize, width, height);
#endif

    try {
        // Validate parameters
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid texture index for upload: %d", textureIndex);
            return false;
        }

        if (!textureData || dataSize == 0 || width == 0 || height == 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid texture data parameters for upload.");
            return false;
        }

        // Check if texture resource exists
        if (!m_d3d12Textures[textureIndex]) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Texture resource %d not created for upload.", textureIndex);
            return false;
        }

        // Calculate texture layout information
        D3D12_RESOURCE_DESC textureDesc = m_d3d12Textures[textureIndex]->GetDesc();
        UINT numSubresources = 1;  // Assume single mip level for now

        // Get required size for upload buffer
        UINT64 uploadBufferSize = 0;
        m_d3d12Device->GetCopyableFootprints(
            &textureDesc,                                                       // Resource description
            0,                                                                  // First subresource
            numSubresources,                                                    // Number of subresources
            0,                                                                  // Base offset
            nullptr,                                                            // Layouts (not needed for size calculation)
            nullptr,                                                            // Num rows (not needed for size calculation)
            nullptr,                                                            // Row sizes (not needed for size calculation)
            &uploadBufferSize                                                   // Total size required
        );

        // Create upload buffer
        ComPtr<ID3D12Resource> uploadBuffer;
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &uploadHeapProps,                                                   // Upload heap properties
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &uploadBufferDesc,                                                  // Buffer description
            D3D12_RESOURCE_STATE_GENERIC_READ,                                  // Initial state for upload heap
            nullptr,                                                            // No optimized clear value
            IID_PPV_ARGS(&uploadBuffer)                                         // Output upload buffer
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create upload buffer for texture data.");
            return false;
        }

        // Set upload buffer name for debugging
        std::wstring uploadBufferName = L"DX12Renderer_TextureUploadBuffer_" + std::to_wstring(textureIndex);
        uploadBuffer->SetName(uploadBufferName.c_str());

        // Map upload buffer and copy texture data
        void* mappedData = nullptr;
        CD3DX12_RANGE readRange(0, 0);  // We won't read from this resource on the CPU
        hr = uploadBuffer->Map(0, &readRange, &mappedData);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to map upload buffer for texture data.");
            return false;
        }

        // Get detailed layout information for data copying
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
        UINT numRows;
        UINT64 rowSizeInBytes;
        m_d3d12Device->GetCopyableFootprints(
            &textureDesc,                                                       // Resource description
            0,                                                                  // First subresource
            1,                                                                  // Single subresource
            0,                                                                  // Base offset
            &layout,                                                            // Layout information
            &numRows,                                                           // Number of rows
            &rowSizeInBytes,                                                    // Row size in bytes
            nullptr                                                             // Total size (already calculated)
        );

        // Calculate bytes per pixel based on format
        UINT bytesPerPixel = 4;  // Default to 4 bytes (RGBA)
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            bytesPerPixel = 4;
            break;
        case DXGI_FORMAT_R8G8_UNORM:
            bytesPerPixel = 2;
            break;
        case DXGI_FORMAT_R8_UNORM:
            bytesPerPixel = 1;
            break;
        case DXGI_FORMAT_BC1_UNORM:
            bytesPerPixel = 0;  // Block compression - handle separately
            break;
        case DXGI_FORMAT_BC3_UNORM:
            bytesPerPixel = 0;  // Block compression - handle separately
            break;
        default:
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Unknown texture format for upload: %d", format);
            bytesPerPixel = 4;  // Default fallback
            break;
        }

        // Copy texture data row by row to handle pitch differences
        const BYTE* srcData = static_cast<const BYTE*>(textureData);
        BYTE* dstData = static_cast<BYTE*>(mappedData) + layout.Offset;

        if (bytesPerPixel > 0) {
            // Uncompressed format
            UINT srcRowPitch = width * bytesPerPixel;
            UINT dstRowPitch = static_cast<UINT>(layout.Footprint.RowPitch);

            for (UINT row = 0; row < height; ++row) {
                memcpy(
                    dstData + row * dstRowPitch,                                // Destination row
                    srcData + row * srcRowPitch,                                // Source row
                    min(srcRowPitch, static_cast<UINT>(rowSizeInBytes))        // Copy size (minimum of source and destination)
                );
            }
        }
        else {
            // Block compressed format - copy entire data block
            memcpy(dstData, srcData, min(dataSize, static_cast<size_t>(uploadBufferSize)));
        }

        // Unmap the upload buffer
        uploadBuffer->Unmap(0, nullptr);

        // Create command allocator and command list for texture upload
        ComPtr<ID3D12CommandAllocator> uploadAllocator;
        hr = m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create upload command allocator.");
            return false;
        }

        ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
        hr = m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadCommandList));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create upload command list.");
            return false;
        }

        // Transition texture to copy destination state
        CD3DX12_RESOURCE_BARRIER copyDestBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_d3d12Textures[textureIndex].Get(),
            D3D12_RESOURCE_STATE_COMMON,                                        // Assume common initial state
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        uploadCommandList->ResourceBarrier(1, &copyDestBarrier);

        // Copy data from upload buffer to texture
        CD3DX12_TEXTURE_COPY_LOCATION dstLocation(m_d3d12Textures[textureIndex].Get(), 0);
        CD3DX12_TEXTURE_COPY_LOCATION srcLocation(uploadBuffer.Get(), layout);
        uploadCommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

        // Transition texture to shader resource state
        CD3DX12_RESOURCE_BARRIER shaderResourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_d3d12Textures[textureIndex].Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        uploadCommandList->ResourceBarrier(1, &shaderResourceBarrier);

        // Close and execute the upload command list
        hr = uploadCommandList->Close();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to close upload command list.");
            return false;
        }

        ID3D12CommandList* uploadLists[] = { uploadCommandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, uploadLists);

        // Wait for upload to complete
        WaitForGPUToFinish();

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture %d data uploaded successfully. Upload buffer size: %llu bytes",
            textureIndex, uploadBufferSize);
#endif

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in UploadTextureData: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Generate Mipmaps for DirectX 12 Texture
//-----------------------------------------
bool DX12Renderer::GenerateMipmaps(int textureIndex) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Generating mipmaps for texture %d", textureIndex);
#endif

    try {
        // Validate texture index
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid texture index for mipmap generation: %d", textureIndex);
            return false;
        }

        // Check if texture resource exists
        if (!m_d3d12Textures[textureIndex]) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Texture resource %d not available for mipmap generation.", textureIndex);
            return false;
        }

        // Get texture description
        D3D12_RESOURCE_DESC textureDesc = m_d3d12Textures[textureIndex]->GetDesc();

        // Check if texture has multiple mip levels
        if (textureDesc.MipLevels <= 1) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Texture %d has only one mip level, no mipmaps to generate.", textureIndex);
            return true;  // Not an error, just nothing to do
        }

        // For DirectX 12, mipmap generation typically requires:
        // 1. A compute shader to downsample each mip level
        // 2. UAV (Unordered Access View) for each mip level
        // 3. Multiple dispatch calls to generate each level

        // For now, we'll implement a placeholder that logs the requirement
        // Full implementation would require a compute shader and additional descriptor heaps

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Mipmap generation for texture %d requires compute shader implementation. MipLevels: %d",
            textureIndex, textureDesc.MipLevels);
#endif

        // TODO: Implement compute shader-based mipmap generation
        // This would involve:
        // 1. Creating UAVs for each mip level
        // 2. Loading a compute shader for downsampling
        // 3. Dispatching compute work for each mip level
        // 4. Proper resource state transitions

        return true;  // Return true for now to avoid blocking texture loading
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in GenerateMipmaps: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Optimize Texture Memory Layout for DirectX 12
//-----------------------------------------
void DX12Renderer::OptimizeTextureMemory() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Optimizing texture memory layout...");
#endif

    try {
        // Analyze current texture memory usage
        UINT64 totalTextureMemory = 0;
        UINT loadedTextureCount = 0;
        UINT unloadedTextureCount = 0;

        // Analyze 3D textures
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; ++i) {
            if (m_d3d12Textures[i]) {
                D3D12_RESOURCE_DESC desc = m_d3d12Textures[i]->GetDesc();

                // Calculate approximate memory usage
                UINT64 textureSize = desc.Width * desc.Height * 4;  // Assume 4 bytes per pixel

                // Adjust for compressed formats
                switch (desc.Format) {
                case DXGI_FORMAT_BC1_UNORM:
                    textureSize = (desc.Width * desc.Height) / 2;           // BC1 is 4:1 compression
                    break;
                case DXGI_FORMAT_BC3_UNORM:
                    textureSize = desc.Width * desc.Height;                 // BC3 is 2:1 compression
                    break;
                default:
                    // Keep default calculation
                    break;
                }

                // Account for mip levels
                if (desc.MipLevels > 1) {
                    textureSize = static_cast<UINT64>(textureSize * 1.33f);     // Approximate 33% overhead for mipmaps
                }

                totalTextureMemory += textureSize;
                loadedTextureCount++;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 3D Texture %d - Size: %llu bytes, Dimensions: %lldx%d, Format: %d, Mips: %d",
                    i, textureSize, desc.Width, desc.Height, desc.Format, desc.MipLevels);
#endif
            }
            else {
                unloadedTextureCount++;
            }
        }

        // Analyze 2D textures (through compatibility layer)
        UINT loaded2DTextures = 0;
        for (int i = 0; i < MAX_TEXTURE_BUFFERS; ++i) {
            if (m_d2dTextures[i]) {
                loaded2DTextures++;

                // Get 2D texture size
                D2D1_SIZE_F size = m_d2dTextures[i]->GetSize();
                UINT64 texture2DSize = static_cast<UINT64>(size.width * size.height * 4);  // Assume BGRA format
                totalTextureMemory += texture2DSize;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D Texture %d - Size: %llu bytes, Dimensions: %.0fx%.0f",
                    i, texture2DSize, size.width, size.height);
#endif
            }
        }

        // Log memory usage statistics
        float totalMemoryMB = static_cast<float>(totalTextureMemory) / (1024.0f * 1024.0f);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture Memory Analysis:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Total Memory Used: %.2f MB (%llu bytes)", totalMemoryMB, totalTextureMemory);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  - 3D Textures Loaded: %d/%d", loadedTextureCount, MAX_TEXTURE_BUFFERS_3D);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  - 2D Textures Loaded: %d/%d", loaded2DTextures, MAX_TEXTURE_BUFFERS);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Unused Texture Slots: %d", unloadedTextureCount + (MAX_TEXTURE_BUFFERS - loaded2DTextures));
#endif

        // Optimization suggestions based on memory usage
        if (totalMemoryMB > 500.0f) {  // More than 500MB
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: High texture memory usage detected. Consider texture compression or resolution reduction.");
        }

        if (loadedTextureCount < MAX_TEXTURE_BUFFERS_3D / 4) {  // Less than 25% usage
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Low texture memory usage - memory layout is efficient.");
        }

        // Perform garbage collection on unused texture slots if needed
        // This could involve compacting the texture array or releasing unused descriptors

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture memory optimization completed.");
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in OptimizeTextureMemory: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Preload Textures for DirectX 12 Performance
//-----------------------------------------
bool DX12Renderer::PreloadTextures(const std::vector<std::wstring>& textureFilenames) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Preloading %zu textures for performance optimization", textureFilenames.size());
#endif

    try {
        bool allSuccess = true;
        int successCount = 0;
        int failureCount = 0;

        // Preload each texture in the list
        for (size_t i = 0; i < textureFilenames.size() && i < MAX_TEXTURE_BUFFERS_3D; ++i) {
            const std::wstring& filename = textureFilenames[i];

            // Check if file exists before attempting to load
            if (!std::filesystem::exists(filename)) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Preload texture file not found: %s", filename.c_str());
                failureCount++;
                allSuccess = false;
                continue;
            }

            // Find an available texture slot
            int availableSlot = -1;
            for (int slot = 0; slot < MAX_TEXTURE_BUFFERS_3D; ++slot) {
                if (!m_d3d12Textures[slot]) {
                    availableSlot = slot;
                    break;
                }
            }

            if (availableSlot == -1) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: No available texture slots for preloading.");
                failureCount++;
                allSuccess = false;
                continue;
            }

            // Load the texture
            bool loadSuccess = LoadTexture(availableSlot, filename, false);  // false = 3D texture
            if (loadSuccess) {
                successCount++;
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Preloaded texture %s to slot %d", filename.c_str(), availableSlot);
#endif
            }
            else {
                failureCount++;
                allSuccess = false;
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to preload texture: %s", filename.c_str());
            }
        }

        // Update texture resources after preloading
        if (successCount > 0) {
            CreateTextureResources();
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture preloading completed. Success: %d, Failed: %d", successCount, failureCount);
#endif

        return allSuccess;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in PreloadTextures: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Create Texture from Memory Data for DirectX 12
//-----------------------------------------
bool DX12Renderer::CreateTextureFromMemory(int textureIndex, const void* data, size_t dataSize, UINT width, UINT height, DXGI_FORMAT format, bool generateMips) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating texture %d from memory. Size: %zu bytes, Dimensions: %dx%d, Format: %d, GenerateMips: %s",
        textureIndex, dataSize, width, height, format, generateMips ? L"Yes" : L"No");
#endif

    try {
        // Validate parameters
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid texture index for memory creation: %d", textureIndex);
            return false;
        }

        if (!data || dataSize == 0 || width == 0 || height == 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid parameters for texture creation from memory.");
            return false;
        }

        // Calculate mip levels if mipmap generation is requested
        UINT mipLevels = 1;
        if (generateMips) {
            mipLevels = static_cast<UINT>(floor(log2(max(width, height)))) + 1;
        }

        // Create texture resource description
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;             // 2D texture
        textureDesc.Alignment = 0;                                              // Default alignment
        textureDesc.Width = width;                                              // Texture width
        textureDesc.Height = height;                                            // Texture height
        textureDesc.DepthOrArraySize = 1;                                       // Single texture
        textureDesc.MipLevels = mipLevels;                                      // Calculated mip levels
        textureDesc.Format = format;                                            // Specified format
        textureDesc.SampleDesc.Count = 1;                                       // No multisampling
        textureDesc.SampleDesc.Quality = 0;                                     // No multisampling quality
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;                      // Driver-optimized layout
        textureDesc.Flags = generateMips ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE; // UAV for mipmap generation

        // Create the texture resource
        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &defaultHeapProps,                                                  // Default heap for GPU access
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &textureDesc,                                                       // Texture description
            D3D12_RESOURCE_STATE_COPY_DEST,                                     // Initial state for copying
            nullptr,                                                            // No optimized clear value
            IID_PPV_ARGS(&m_d3d12Textures[textureIndex])                       // Output texture resource
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create texture resource from memory.");
            return false;
        }

        // Set texture name for debugging
        std::wstring textureName = L"DX12Renderer_MemoryTexture_" + std::to_wstring(textureIndex);
        m_d3d12Textures[textureIndex]->SetName(textureName.c_str());

        // Upload the texture data
        bool uploadSuccess = UploadTextureData(textureIndex, data, dataSize, width, height, format);
        if (!uploadSuccess) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to upload texture data from memory.");
            m_d3d12Textures[textureIndex].Reset();
            return false;
        }

        // Generate mipmaps if requested
        if (generateMips && mipLevels > 1) {
            bool mipSuccess = GenerateMipmaps(textureIndex);
            if (!mipSuccess) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to generate mipmaps for memory texture, continuing without mipmaps.");
            }
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Texture %d created from memory successfully. MipLevels: %d", textureIndex, mipLevels);
#endif

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateTextureFromMemory: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Batch Load Textures for DirectX 12 Efficiency
//-----------------------------------------
bool DX12Renderer::BatchLoadTextures(const std::vector<std::pair<int, std::wstring>>& textureList, bool is2D) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Batch loading %zu textures (2D: %s)", textureList.size(), is2D ? L"Yes" : L"No");
#endif

    try {
        bool allSuccess = true;
        int successCount = 0;
        int failureCount = 0;

        // Create batch command allocator and command list for efficient uploading
        ComPtr<ID3D12CommandAllocator> batchAllocator;
        HRESULT hr = m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&batchAllocator));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create batch command allocator for texture loading.");
            return false;
        }

        ComPtr<ID3D12GraphicsCommandList> batchCommandList;
        hr = m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, batchAllocator.Get(), nullptr, IID_PPV_ARGS(&batchCommandList));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create batch command list for texture loading.");
            return false;
        }

        // Process each texture in the batch
        for (const auto& texturePair : textureList) {
            int textureIndex = texturePair.first;
            const std::wstring& filename = texturePair.second;

            // Validate texture index based on type
            bool validIndex = is2D ? (textureIndex >= 0 && textureIndex < MAX_TEXTURE_BUFFERS)
                : (textureIndex >= 0 && textureIndex < MAX_TEXTURE_BUFFERS_3D);

            if (!validIndex) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid texture index %d in batch load", textureIndex);
                failureCount++;
                allSuccess = false;
                continue;
            }

            // Check if file exists
            if (!std::filesystem::exists(filename)) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Batch load texture file not found: %s", filename.c_str());
                failureCount++;
                allSuccess = false;
                continue;
            }

            // Load the texture
            bool loadSuccess = LoadTexture(textureIndex, filename, is2D);
            if (loadSuccess) {
                successCount++;
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Batch loaded texture %s to slot %d", filename.c_str(), textureIndex);
#endif
            }
            else {
                failureCount++;
                allSuccess = false;
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to batch load texture: %s", filename.c_str());
            }
        }

        // Close and execute batch command list if any 3D textures were loaded
        if (!is2D && successCount > 0) {
            hr = batchCommandList->Close();
            if (SUCCEEDED(hr)) {
                ID3D12CommandList* batchLists[] = { batchCommandList.Get() };
                m_commandQueue->ExecuteCommandLists(1, batchLists);
                WaitForGPUToFinish();  // Wait for batch upload to complete
            }
        }

        // Update texture resources after batch loading
        if (successCount > 0) {
            if (!is2D) {
                CreateTextureResources();
            }
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Batch texture loading completed. Success: %d, Failed: %d", successCount, failureCount);
#endif

        return allSuccess;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in BatchLoadTextures: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Get Texture Memory Statistics for DirectX 12
//-----------------------------------------
void DX12Renderer::GetTextureMemoryStats(UINT64& totalMemoryUsed, UINT& texturesLoaded, UINT& availableSlots) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Gathering texture memory statistics...");
#endif

    try {
        totalMemoryUsed = 0;
        texturesLoaded = 0;
        availableSlots = 0;

        // Analyze 3D textures
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; ++i) {
            if (m_d3d12Textures[i]) {
                texturesLoaded++;

                // Calculate approximate memory usage
                D3D12_RESOURCE_DESC desc = m_d3d12Textures[i]->GetDesc();
                UINT64 textureSize = desc.Width * desc.Height * 4;  // Assume 4 bytes per pixel

                // Adjust for compressed formats
                switch (desc.Format) {
                case DXGI_FORMAT_BC1_UNORM:
                    textureSize = (desc.Width * desc.Height) / 2;
                    break;
                case DXGI_FORMAT_BC3_UNORM:
                    textureSize = desc.Width * desc.Height;
                    break;
                default:
                    break;
                }

                // Account for mip levels
                if (desc.MipLevels > 1) {
                    textureSize = static_cast<UINT64>(textureSize * 1.33f);
                }

                totalMemoryUsed += textureSize;
            }
            else {
                availableSlots++;
            }
        }

        // Analyze 2D textures (approximate)
        for (int i = 0; i < MAX_TEXTURE_BUFFERS; ++i) {
            if (m_d2dTextures[i]) {
                texturesLoaded++;

                // Estimate 2D texture size
                D2D1_SIZE_F size = m_d2dTextures[i]->GetSize();
                UINT64 texture2DSize = static_cast<UINT64>(size.width * size.height * 4);
                totalMemoryUsed += texture2DSize;
            }
            else {
                availableSlots++;
            }
        }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        float totalMemoryMB = static_cast<float>(totalMemoryUsed) / (1024.0f * 1024.0f);
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Memory Stats - Total: %.2f MB, Loaded: %d, Available: %d",
            totalMemoryMB, texturesLoaded, availableSlots);
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in GetTextureMemoryStats: %s", errorMsg.c_str());

        // Set default values on error
        totalMemoryUsed = 0;
        texturesLoaded = 0;
        availableSlots = MAX_TEXTURE_BUFFERS + MAX_TEXTURE_BUFFERS_3D;
    }
}

//-----------------------------------------
// Validate Texture Resource for DirectX 12
//-----------------------------------------
bool DX12Renderer::ValidateTextureResource(int textureIndex, bool is2D) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Validating texture resource %d (2D: %s)", textureIndex, is2D ? L"Yes" : L"No");
#endif

    try {
        // Validate index range
        if (is2D) {
            if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: 2D texture index %d out of range (0-%d)", textureIndex, MAX_TEXTURE_BUFFERS - 1);
                return false;
            }

            // Check if 2D texture exists
            if (!m_d2dTextures[textureIndex]) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 2D texture %d not loaded", textureIndex);
                return false;
            }

            // Validate 2D texture properties
            D2D1_SIZE_F size = m_d2dTextures[textureIndex]->GetSize();
            if (size.width <= 0 || size.height <= 0) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: 2D texture %d has invalid dimensions: %.0fx%.0f", textureIndex, size.width, size.height);
                return false;
            }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D texture %d validation passed. Size: %.0fx%.0f", textureIndex, size.width, size.height);
#endif
        }
        else {
            if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: 3D texture index %d out of range (0-%d)", textureIndex, MAX_TEXTURE_BUFFERS_3D - 1);
                return false;
            }

            // Check if 3D texture exists
            if (!m_d3d12Textures[textureIndex]) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: 3D texture %d not loaded", textureIndex);
                return false;
            }

            // Validate 3D texture properties
            D3D12_RESOURCE_DESC desc = m_d3d12Textures[textureIndex]->GetDesc();

            // Check dimensions
            if (desc.Width == 0 || desc.Height == 0) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: 3D texture %d has invalid dimensions: %lldx%d", textureIndex, desc.Width, desc.Height);
                return false;
            }

            // Check format
            if (desc.Format == DXGI_FORMAT_UNKNOWN) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: 3D texture %d has unknown format", textureIndex);
                return false;
            }

            // Check if resource is in valid state
            // Note: In a full implementation, we might check the resource state
            // For now, we assume if the resource exists, it's in a valid state

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 3D texture %d validation passed. Size: %lldx%d, Format: %d, Mips: %d",
                textureIndex, desc.Width, desc.Height, desc.Format, desc.MipLevels);
#endif
        }

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in ValidateTextureResource: %s", errorMsg.c_str());
        return false;
    }
}

//-----------------------------------------
// Release Unused Texture Resources for DirectX 12
//-----------------------------------------
void DX12Renderer::ReleaseUnusedTextures() {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Releasing unused texture resources...");
#endif

    try {
        UINT releasedCount = 0;
        UINT64 memoryFreed = 0;

        // Check 3D textures for unused resources
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; ++i) {
            if (m_d3d12Textures[i]) {
                // In a full implementation, we would check:
                // 1. Last access time
                // 2. Reference count
                // 3. Usage frequency
                // 4. Scene requirements

                // For now, we'll implement a simple placeholder
                // that checks if the texture has been accessed recently

                // Example: Release textures that haven't been used in the last 1000 frames
                // This would require tracking last access frame numbers

                // TODO: Implement texture usage tracking and automatic cleanup
                // For now, we'll just log the texture for manual review

                D3D12_RESOURCE_DESC desc = m_d3d12Textures[i]->GetDesc();
                UINT64 textureSize = desc.Width * desc.Height * 4; // Estimate size

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 3D texture %d candidate for cleanup. Size: %llu bytes", i, textureSize);
#endif
            }
        }

        // Check 2D textures for unused resources
        for (int i = 0; i < MAX_TEXTURE_BUFFERS; ++i) {
            if (m_d2dTextures[i]) {
                // Similar logic for 2D textures
                D2D1_SIZE_F size = m_d2dTextures[i]->GetSize();
                UINT64 texture2DSize = static_cast<UINT64>(size.width * size.height * 4);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: 2D texture %d candidate for cleanup. Size: %llu bytes", i, texture2DSize);
#endif
            }
        }

        // Perform memory optimization after cleanup
        OptimizeTextureMemory();

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        if (releasedCount > 0) {
            float memoryFreedMB = static_cast<float>(memoryFreed) / (1024.0f * 1024.0f);
            debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Released %d unused textures, freed %.2f MB", releasedCount, memoryFreedMB);
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: No unused textures found for release.");
        }
#endif
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in ReleaseUnusedTextures: %s", errorMsg.c_str());
    }
}

//-----------------------------------------
// Create Render Texture for DirectX 12
//-----------------------------------------
bool DX12Renderer::CreateRenderTexture(int textureIndex, UINT width, UINT height, DXGI_FORMAT format, bool useAsDepthBuffer) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Creating render texture %d. Size: %dx%d, Format: %d, DepthBuffer: %s",
        textureIndex, width, height, format, useAsDepthBuffer ? L"Yes" : L"No");
#endif

    try {
        // Validate parameters
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid texture index for render texture creation: %d", textureIndex);
            return false;
        }

        if (width == 0 || height == 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid dimensions for render texture creation.");
            return false;
        }

        // Create render texture resource description
        D3D12_RESOURCE_DESC renderTextureDesc = {};
        renderTextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;       // 2D texture
        renderTextureDesc.Alignment = 0;                                        // Default alignment
        renderTextureDesc.Width = width;                                        // Render texture width
        renderTextureDesc.Height = height;                                      // Render texture height
        renderTextureDesc.DepthOrArraySize = 1;                                 // Single texture
        renderTextureDesc.MipLevels = 1;                                        // Single mip level for render target
        renderTextureDesc.Format = format;                                      // Specified format
        renderTextureDesc.SampleDesc.Count = 1;                                 // No multisampling
        renderTextureDesc.SampleDesc.Quality = 0;                               // No multisampling quality
        renderTextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;                // Driver-optimized layout

        // Set appropriate flags based on usage
        if (useAsDepthBuffer) {
            renderTextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;  // Depth stencil usage
        }
        else {
            renderTextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;  // Render target usage
        }

        // Define clear value for optimal performance
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = format;
        if (useAsDepthBuffer) {
            clearValue.DepthStencil.Depth = 1.0f;                               // Clear to maximum depth
            clearValue.DepthStencil.Stencil = 0;                                // Clear stencil to zero
        }
        else {
            clearValue.Color[0] = 0.0f;                                         // Clear to black
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 1.0f;                                         // Full alpha
        }

        // Create the render texture resource
        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &defaultHeapProps,                                                  // Default heap for GPU access
            D3D12_HEAP_FLAG_NONE,                                               // No special heap flags
            &renderTextureDesc,                                                 // Resource description
            useAsDepthBuffer ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET, // Initial state
            &clearValue,                                                        // Optimal clear value
            IID_PPV_ARGS(&m_d3d12Textures[textureIndex])                        // Output texture resource
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create render texture resource.");
            return false;
        }

        // Set render texture name for debugging
        std::wstring textureName = L"DX12Renderer_RenderTexture_" + std::to_wstring(textureIndex) +
            (useAsDepthBuffer ? L"_Depth" : L"_Color");
        m_d3d12Textures[textureIndex]->SetName(textureName.c_str());

        // Create appropriate view for the render texture
        if (useAsDepthBuffer) {
            // Create DSV for depth buffer usage
            // Note: This would require expanding the DSV heap or using a separate heap
            // For now, we'll just log the requirement
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Depth render texture created. DSV creation would be required for usage.");
        }
        else {
            // Create RTV for color render target usage
            // Note: This would require expanding the RTV heap or using a separate heap
            // For now, we'll just log the requirement
            debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Color render texture created. RTV creation would be required for usage.");
        }

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Render texture %d created successfully.", textureIndex);
        #endif

        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in CreateRenderTexture: %s", errorMsg.c_str());
        return false;
    }
}


//---------------------------------------------
// Main Rendering Frame Function for DirectX 12
//---------------------------------------------
void DX12Renderer::RenderFrame()
{
    // SAFE-GUARDS - Early exit conditions for invalid states
    if (bHasCleanedUp || !m_d3d12Device || !m_commandQueue || !m_constantBuffer) return;
    if (threadManager.threadVars.bIsShuttingDown.load() || bIsMinimized.load() || threadManager.threadVars.bIsResizing.load() || 
        !bIsInitialized.load() || threadManager.threadVars.bIsRendering.load()) return;

    try
    {
        // Try to acquire the render frame lock with a 10ms timeout
        if (!threadManager.TryLock(renderFrameLockName, 10)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Could not acquire render frame lock - timeout reached");
            return;
        }

        // Initialize frame timing and window metrics
        HRESULT hr = S_OK;
        HWND hWnd = hwnd;

        // Get window rectangle for viewport calculations
        D3D12_VIEWPORT viewport = {};
        RECT rc;
        POINT cursorPos;

        if (!winMetrics.isFullScreen)
        {
            GetClientRect(hWnd, &rc);                                           // Get client area for windowed mode
        }
        else
        {
            rc = winMetrics.monitorFullArea;                                    // Use full monitor area for fullscreen
        }

        // Calculate viewport dimensions
        float width = float(rc.right - rc.left);
        float height = float(rc.bottom - rc.top);
        viewport.Width = width;
        viewport.Height = height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;

        // Check the status of the rendering thread
#ifdef RENDERER_IS_THREAD
        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) && (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            if (status == ThreadStatus::Paused)
            {
                Sleep(1);                                                       // Small delay during pause
                continue;
            }
#endif

            // Check DirectX 12 device health
            if (m_d3d12Device)
            {
                // DirectX 12 doesn't have GetDeviceRemovedReason like DirectX 11
                // Instead, we check if critical resources are still valid
                if (!m_swapChain || !m_commandQueue || !m_fence)
                {
                    if (!threadManager.threadVars.bIsResizing.load() && !sysUtils.IsWindowMinimized())
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Critical resources invalid. Attempting recovery.");
                        threadManager.threadVars.bIsResizing.store(true);
                        Resize(iOrigWidth, iOrigHeight);
                        ResumeLoader();
                        threadManager.threadVars.bIsResizing.store(false);
                        threadManager.RemoveLock(renderFrameLockName);
                        return;
                    }
                    else
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Critical resources invalid but window minimized or resizing. Skipping recovery.");
                    }
                }
            }

            // Mark that we are now rendering
            threadManager.threadVars.bIsRendering.store(true);

            // Wait for the previous frame to complete before starting new frame
            WaitForPreviousFrame();

            // Reset command list for new frame recording
            ResetCommandList();

            // Update frame timing for delta time calculations
            static auto myLastTime = std::chrono::high_resolution_clock::now();
            auto myCurrentTime = std::chrono::high_resolution_clock::now();
            myLastTime = myCurrentTime;

            // Get current frame start time for delta time calculation
            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            // Get and scale mouse coordinates for current frame
            GetCursorPos(&cursorPos);
            ScreenToClient(hWnd, &cursorPos);
            myMouseCoords.x = cursorPos.x;
            myMouseCoords.y = cursorPos.y;

            // Scale the mouse coordinates for different resolution modes
            auto [scaledX, scaledY] = sysUtils.ScaleMouseCoordinates(cursorPos.x, cursorPos.y, iOrigWidth, iOrigHeight, width, height);
            float x = float(scaledX);
            float y = float(scaledY);

            // Set the graphics root signature for this frame
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

            // Set descriptor heaps for shader resource binding
            ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
            m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

            // Set viewport and scissor rectangle for rendering
            m_commandList->RSSetViewports(1, &viewport);
            D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // Transition the current render target from present to render target state
            TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

            // Get render target and depth stencil handles for this frame
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_frameContexts[m_frameIndex].rtvHandle;
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);

            // Set render targets for this frame
            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

            // Clear the render target and depth stencil buffer
            const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };             // Black clear color
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            // Update camera view matrix for this frame
            myCamera.UpdateViewMatrix();

            // Update constant buffers with current frame data
            UpdateConstantBuffers();

            // Bind constant buffer views to the graphics pipeline
            if (m_constantBuffer) {
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER,
                    m_constantBuffer->GetGPUVirtualAddress());
            }

            if (m_globalLightBuffer) {
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER,
                    m_globalLightBuffer->GetGPUVirtualAddress());
            }

            // Set primitive topology for triangle rendering
            m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // We now need to determine which Scene we are to render
            switch (scene.stSceneType)
            {
            case SceneType::SCENE_SPLASH:
            {
                // Transition render target back to present for 2D rendering
                TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT);

                // Close command list for 3D rendering
                CloseCommandList();
                ExecuteCommandList();

                // Try to acquire the Direct2D render lock for 2D operations
                if (threadManager.TryLock(D2DLockName, 100))
                {
                    // Begin Direct2D rendering operations
                    if (m_d2dContext) {
                        m_d2dContext->BeginDraw();

                        // Present Splash Screen using 2D compatibility layer
                        if (IsDX11CompatibilityAvailable()) {
                            // Use DirectX 11 on 12 for 2D rendering
                            // Implementation will be added in Step 6
                        }

                        // End Direct2D rendering
                        HRESULT d2dResult = m_d2dContext->EndDraw();
                        if (FAILED(d2dResult)) {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Direct2D EndDraw failed in splash scene.");
                        }
                    }

                    // Release the Direct2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Could not acquire D2D render lock - skipping 2D operations");
                }

                break;
            }

            case SceneType::SCENE_INTRO_MOVIE:
            {
                // Transition render target back to present for 2D rendering
                TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT);

                // Close command list for 3D rendering
                CloseCommandList();
                ExecuteCommandList();

                // Try to acquire the Direct2D render lock for movie playback
                if (threadManager.TryLock(D2DLockName, 100))
                {
                    // Begin Direct2D rendering operations
                    if (m_d2dContext) {
                        m_d2dContext->BeginDraw();

                        // Check if movie is playing
                        if (moviePlayer.IsPlaying()) {
                            // Update the movie frame
                            moviePlayer.UpdateFrame();

                            // Render the movie to fill the screen
                            // Movie rendering implementation will be added in Step 6
                            // moviePlayer.Render(Vector2(0, 0), Vector2(iOrigWidth, iOrigHeight));

                            // Check for user input to skip movie
                            if (GetAsyncKeyState(' ') & 0x8000)
                            {
                                moviePlayer.Stop(); // Stop playback to switch scene
                            }
                        }

                        // End Direct2D rendering
                        HRESULT d2dResult = m_d2dContext->EndDraw();
                        if (FAILED(d2dResult)) {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Direct2D EndDraw failed in movie scene.");
                        }
                    }

                    // Release the Direct2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Could not acquire D2D render lock - skipping movie operations");
                }

                break;
            }

            case SceneType::SCENE_GAMEPLAY:
            {
                // 3D Rendering Pipeline for Gameplay Scene

                // Ensure all loading is complete before rendering models and lighting
                if (threadManager.threadVars.bLoaderTaskFinished.load())
                {
                    // Debug input for wireframe mode toggle
#if defined(_DEBUG_RENDER_WIREFRAME_)
    // Wireframe mode would require separate pipeline state in DirectX 12
    // This will be implemented later
#endif

// Simple triangle test for pipeline validation (debug only)
#if defined(_DEBUG_DX12RENDERER_) && defined(_SIMPLE_TRIANGLE_) && defined(_DEBUG)
                    TestDrawTriangle(); // Test triangle for rendering (debug purposes)
#endif

                    // Render all loaded 3D models
                    for (int i = 0; i < MAX_MODELS; ++i)
                    {
                        if (scene.scene_models[i].m_isLoaded)
                        {
                            // Update model information for DirectX 12 rendering
                            scene.scene_models[i].m_modelInfo.fxActive = false; // Force this off for now
                            scene.scene_models[i].m_modelInfo.viewMatrix = myCamera.GetViewMatrix();
                            scene.scene_models[i].m_modelInfo.projectionMatrix = myCamera.GetProjectionMatrix();
                            scene.scene_models[i].m_modelInfo.cameraPosition = myCamera.GetPosition();

                            // Update animation for this model
                            scene.scene_models[i].UpdateAnimation(deltaTime);

                            // Render model using DirectX 12 command list
                            // Model rendering adaptation for DirectX 12 will be implemented in subsequent steps
                            // scene.scene_models[i].RenderDX12(m_commandList.Get(), deltaTime);
                        }
                    }
                }

                // Animate lights per frame
                // lightsManager.AnimateLights(deltaTime); // TODO: Replace with real deltaTime

                // Transition render target back to present state after 3D rendering
                TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT);

                // Close and execute 3D rendering commands
                CloseCommandList();
                ExecuteCommandList();

                break;
            }
            }

            // 2D Rendering Layer - Universal for all scenes
            if (m_d2dContext && IsDX11CompatibilityAvailable())
            {
                // Check status of Direct2D to see if available for use
                if (threadManager.TryLock(D2DLockName, 100))
                {
                    // Begin Direct2D drawing operations
                    m_d2dContext->BeginDraw();

                    // Scene-specific 2D rendering
                    if ((!threadManager.threadVars.bIsShuttingDown.load()) &&
                        (!bIsMinimized.load()) &&
                        (!threadManager.threadVars.bIsResizing.load()) &&
                        (bIsInitialized.load()))
                    {
                        switch (scene.stSceneType)
                        {
                        case SceneType::SCENE_INTRO:
                        {
                            // Ensure all loading is complete before rendering intro elements
                            if (threadManager.threadVars.bLoaderTaskFinished.load())
                            {
                                // Set camera for intro scene
                                myCamera.SetYawPitch(0.285f, -0.22f);

                                // 2D intro scene rendering will be implemented in Step 6
                                // Draw background image, logo, and effects
                            }
                            break;
                        }

                        case SceneType::SCENE_INTRO_MOVIE:
                        {
                            // Movie scene 2D overlay rendering
                            if (moviePlayer.IsPlaying() && (!threadManager.threadVars.bLoaderTaskFinished.load()))
                            {
                                // 2D movie overlay rendering will be implemented in Step 6
                                // Draw company logo overlay
                            }
                            break;
                        }
                        }
                    }

                    // FPS Display for debugging and performance monitoring
                    if (USE_FPS_DISPLAY)
                    {
                        static auto lastFrameTime = std::chrono::steady_clock::now();
                        static auto lastFPSTime = lastFrameTime;
                        static int frameCounter = 0;

                        auto currentTime = std::chrono::steady_clock::now();
                        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
                        float elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count();

                        lastFrameTime = currentTime;
                        frameCounter++;

                        // Update FPS calculation every second
                        if (elapsedForFPS >= 1.0f)
                        {
                            fps = static_cast<float>(frameCounter) / elapsedForFPS;
                            frameCounter = 0;
                            lastFPSTime = currentTime;
                        }

                        // Get camera coordinates for display
                        XMFLOAT3 Coords = myCamera.GetPosition();

                        // Build FPS and debug information string
                        std::wstring fpsText = L"FPS: " + std::to_wstring(fps) + L"\nMOUSE: x" + std::to_wstring(cursorPos.x) + L", y" + std::to_wstring(cursorPos.y);
                        fpsText = fpsText + L"\nCamera X: " + std::to_wstring(Coords.x) + L", Y: " + std::to_wstring(Coords.y) + L", Z: " + std::to_wstring(Coords.z) +
                            L", Yaw: " + std::to_wstring(myCamera.m_yaw) + L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n";
                        fpsText = fpsText + L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n";
                        fpsText = fpsText + L"Renderer: DirectX 12\n";

                        // Draw FPS text using DirectX 11 compatibility layer
                        // Text rendering implementation will be added in Step 6
                        // DrawMyText(fpsText, Vector2(0, 0), MyColor(255, 255, 255, 255), 10.0f);
                    }

                    // Loading indicator animation
                    if (!threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        static int delay = 0;
                        static int loadIndex = 0;
                        static int iPosX = 0;

                        delay++;
                        if (delay > 5)
                        {
                            loadIndex++;
                            if (loadIndex > 9) { loadIndex = 0; }
                            delay = 0;
                        }

                        // Loading circle animation will be implemented in Step 6
                        // iPosX = loadIndex << 5;
                        // Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE, width - 32, height - 32, iPosX, 0, 32, 32);
                    }

                    // Apply 2D Effects and GUI rendering
                    fxManager.Render2D();                                       // Render 2D effects
                    guiManager.Render();                                        // Render GUI windows

                    // Render Mouse Cursor
                    // Mouse cursor rendering will be implemented in Step 6
                    // Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR, cursorPos.x, cursorPos.y);

                    // End Direct2D drawing operations
                    try
                    {
                        HRESULT hr = m_d2dContext->EndDraw();
                        if (FAILED(hr))
                        {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Direct2D EndDraw failed.");
                        }

                        // Render post-processing effects after normal rendering but before present
                        fxManager.Render();
                    }
                    catch (const std::exception& e)
                    {
                        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
                        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Exception in Direct2D EndDraw: %s", errorMsg.c_str());
                    }

                    // Release the Direct2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Could not acquire D2D render lock - skipping 2D operations");
                }
            }

            // Present the frame to the display
            PresentFrame();

            // Move to the next frame for double buffering
            MoveToNextFrame();

            // Mark that we are no longer rendering    
            threadManager.threadVars.bIsRendering.store(false);

#ifdef RENDERER_IS_THREAD
        } // End of while loop for thread status check
#endif

        // Make sure to remove the lock even if an exception occurs
        threadManager.RemoveLock(renderFrameLockName);
    }
    catch (const std::exception& e)
    {
        // Make sure to remove the lock even if an exception occurs
        threadManager.RemoveLock(renderFrameLockName);
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Exception in RenderFrame: %s", errorMsg.c_str());
    }

#ifdef RENDERER_IS_THREAD
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Render Thread Exiting.");
#endif
}





#endif // defined(__USE_DIRECTX_12__)
