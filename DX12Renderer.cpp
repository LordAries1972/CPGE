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
    #include "DX12FXManager.h"
    #include "GUIManager.h"
    #include "DX12Models.h"
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
    #pragma comment(lib, "d3d11.lib")
    #pragma comment(lib, "d2d1.lib")
    #pragma comment(lib, "dwrite.lib")

    // Minimal DDS header structure for parsing DDS texture files
    #pragma pack(push, 1)
    struct DDS_PIXELFORMAT {
        DWORD dwSize, dwFlags, dwFourCC;
        DWORD dwRGBBitCount;
        DWORD dwRBitMask, dwGBitMask, dwBBitMask, dwABitMask;
    };
    struct DDS_HEADER {
        DWORD           dwSize, dwFlags, dwHeight, dwWidth;
        DWORD           dwPitchOrLinearSize, dwDepth, dwMipMapCount;
        DWORD           dwReserved1[11];
        DDS_PIXELFORMAT ddspf;
        DWORD           dwCaps, dwCaps2, dwCaps3, dwCaps4, dwReserved2;
    };
    #pragma pack(pop)

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

    // Initialize plain-data arrays to zero (ComPtr arrays default-construct to nullptr; do NOT SecureZeroMemory them)
    SecureZeroMemory(&m_d3d12Textures, sizeof(m_d3d12Textures));
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
                // GPU-Based Validation is disabled for the WIP DX12 pipeline — it raises
                // hard exceptions (via RaiseException) on upload-heap CBV writes that violate
                // the DATA_STATIC contract, crashing the process before errors can be logged.
                // Re-enable once constant buffer update patterns are finalised.
                debugController1->SetEnableGPUBasedValidation(FALSE);
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

        // Determine effective frame count from config (triple=3 / double=2)
        m_effectiveFrameCount = (config.myConfig.buffering != 0) ? FrameCount : 2u;

        // Create the DirectX 12 device — request FL 11.0 minimum so that GPUs only
        // supporting DX12 at feature level 11.1 (many Intel/older AMD) are not
        // rejected.  The actual device is created at the adapter's maximum level.
        hr = D3D12CreateDevice(bestAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_d3d12Device));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create DirectX 12 device.");
            ThrowError("DirectX 12 device creation failed");
            return;
        }

        // Set the device name for debugging purposes
        m_d3d12Device->SetName(L"DX12Renderer_MainDevice");

        // Mark DirectX 12 as available in compatibility context
        m_dx11Dx12Compat.bDX12Available = true;

        // Query GPU capabilities for adaptive behaviour on low-end hardware.
        // Results are stored as members and drive runtime quality decisions.
        {
            D3D_FEATURE_LEVEL candidateLevels[] = {
                D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
            };
            D3D12_FEATURE_DATA_FEATURE_LEVELS levelData = {};
            levelData.NumFeatureLevels        = _countof(candidateLevels);
            levelData.pFeatureLevelsRequested = candidateLevels;
            if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(
                    D3D12_FEATURE_FEATURE_LEVELS, &levelData, sizeof(levelData))))
                m_maxFeatureLevel = levelData.MaxSupportedFeatureLevel;

            D3D12_FEATURE_DATA_ARCHITECTURE arch = {};
            if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(
                    D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch))))
                m_isUMA = (arch.UMA == TRUE);

            D3D12_FEATURE_DATA_D3D12_OPTIONS opts = {};
            if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(
                    D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts))))
                m_resourceBindingTier = opts.ResourceBindingTier;

            DXGI_ADAPTER_DESC3 adDesc = {};
            if (SUCCEEDED(bestAdapter->GetDesc3(&adDesc)))
            {
                m_dedicatedVRAMMB    = adDesc.DedicatedVideoMemory / (1024 * 1024);
                m_sharedSystemMemMB  = adDesc.SharedSystemMemory   / (1024 * 1024);
            }
            // Low-end: < 2 GB dedicated VRAM, or UMA (integrated GPU sharing system RAM)
            m_isLowEndGPU = (m_dedicatedVRAMMB < 2048 || m_isUMA);

            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"DX12Renderer: GPU Caps: VRAM: %llu MB, Shared: %llu MB, UMA: %s, LowEnd: %s, BindingTier: %d",
                m_dedicatedVRAMMB, m_sharedSystemMemMB,
                m_isUMA       ? L"Yes" : L"No",
                m_isLowEndGPU ? L"Yes" : L"No",
                static_cast<int>(m_resourceBindingTier));

            if (m_isLowEndGPU)
                debug.logLevelMessage(LogLevel::LOG_WARNING,
                    L"DX12Renderer: Low-end GPU detected - reduced VRAM or integrated/UMA architecture.");
        }

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
        swapChainDesc.BufferCount = m_effectiveFrameCount;                      // 3=triple / 2=double per config
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
        cbvSrvUavHeapDesc.NumDescriptors = DX12_MODEL_TEXTURE_HEAP_BASE + DX12_MODEL_TEXTURE_HEAP_CAPACITY + FrameCount; // +FrameCount for D2D off-screen composite SRVs
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

        // Create render target views for each active frame buffer
        for (UINT i = 0; i < m_effectiveFrameCount; ++i) {
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
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,                                                         // Default heap for GPU access
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

            // Create composite allocator (used for the D2D off-screen composite pass at frame end)
            hr = m_d3d12Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_frameContexts[i].compositeAllocator));
            if (SUCCEEDED(hr)) {
                std::wstring compAllocName = L"DX12Renderer_CompositeAllocator_" + std::to_wstring(i);
                m_frameContexts[i].compositeAllocator->SetName(compAllocName.c_str());
            }

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

            // Test DX12 compatibility at FL 11.0 minimum to include low-end adapters
            ComPtr<ID3D12Device> testDevice;
            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice));
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
        // Set up info queue for detailed debug messages; store in m_infoQueue so
        // DrainInfoQueue() can forward validation errors to the game log on demand.
        if (SUCCEEDED(m_d3d12Device.As(&m_infoQueue))) {
            // Only break on data corruption — breaking on ERROR or WARNING raises
            // RaiseException() which crashes the process on any validation hit,
            // including false positives in a WIP pipeline. Errors/warnings are still
            // captured by the storage filter and visible in the debug output.
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      FALSE);
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    FALSE);

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

            m_infoQueue->PushStorageFilter(&filter);

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
// DrainInfoQueue
// Reads every pending message from the D3D12 info queue and routes it to the
// game log. Call after any important DX12 failure to get the exact validation
// error that the runtime produced, not just the numeric HRESULT.
//-----------------------------------------
#ifdef _DEBUG
void DX12Renderer::DrainInfoQueue()
{
    if (!m_infoQueue) return;

    const UINT64 count = m_infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T msgSize = 0;
        m_infoQueue->GetMessage(i, nullptr, &msgSize);
        if (msgSize == 0) continue;

        std::vector<BYTE> buf(msgSize);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(m_infoQueue->GetMessage(i, msg, &msgSize))) continue;

        // Convert the ASCII description to a wide string for our logger
        const SIZE_T descLen = msg->DescriptionByteLength > 0 ? msg->DescriptionByteLength - 1 : 0;
        std::wstring wDesc(msg->pDescription, msg->pDescription + descLen);

        LogLevel level = LogLevel::LOG_WARNING;
        switch (msg->Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION: level = LogLevel::LOG_CRITICAL; break;
        case D3D12_MESSAGE_SEVERITY_ERROR:      level = LogLevel::LOG_ERROR;    break;
        case D3D12_MESSAGE_SEVERITY_WARNING:    level = LogLevel::LOG_WARNING;  break;
        default:                                level = LogLevel::LOG_DEBUG;    break;
        }
        debug.logDebugMessage(level, L"[D3D12 Validation] %s", wDesc.c_str());
    }
    m_infoQueue->ClearStoredMessages();
}
#endif

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
        // Define root parameters for the graphics pipeline (8 entries: b0-b5 CBVs + texture SRV table + b6 shadow CBV)
        CD3DX12_ROOT_PARAMETER1 rootParameters[8];

        // Root Parameter 0: b0 — ConstantBuffer (camera/view/world matrices)
        rootParameters[DX12_ROOT_PARAM_CONST_BUFFER].InitAsConstantBufferView(
            0,                                                                  // Register b0
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated every frame via Map/Unmap
            D3D12_SHADER_VISIBILITY_ALL                                         // Used by both VS and PS
        );

        // Root Parameter 1: b1 — LightBuffer (scene-local lights)
        rootParameters[DX12_ROOT_PARAM_LIGHT_BUFFER].InitAsConstantBufferView(
            1,                                                                  // Register b1
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated every frame via Map/Unmap
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
        );

        // Root Parameter 2: b2 — DebugBuffer (pixel shader debug mode)
        rootParameters[DX12_ROOT_PARAM_DEBUG_BUFFER].InitAsConstantBufferView(
            2,                                                                  // Register b2
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated every frame via Map/Unmap
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
        );

        // Root Parameter 3: b3 — GlobalLightBuffer (global / world lights)
        rootParameters[DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER].InitAsConstantBufferView(
            3,                                                                  // Register b3
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated every frame via Map/Unmap
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
        );

        // Root Parameter 4: b4 — MaterialBuffer (PBR material properties)
        rootParameters[DX12_ROOT_PARAM_MATERIAL_BUFFER].InitAsConstantBufferView(
            4,                                                                  // Register b4
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated every frame via Map/Unmap
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
        );

        // Root Parameter 5: b5 — EnvBuffer (environment intensity / tint / fresnel)
        // REQUIRED by ModelPShader.hlsl: cbuffer EnvBuffer : register(b5)
        rootParameters[DX12_ROOT_PARAM_ENVIRONMENT_BUFFER].InitAsConstantBufferView(
            5,                                                                  // Register b5
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated per-scene
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
        );

        // Root Parameter 6: Descriptor table — t0-t8 SRV textures
        // t0=diffuse, t1=normal, t2=metallic, t3=roughness, t4=AO, t5=envCube, t6=gloss, t7=emissive, t8=shadowMap
        CD3DX12_DESCRIPTOR_RANGE1 textureRanges[1];
        textureRanges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,                                    // Shader Resource View
            9,                                                                  // t0-t8 (9 descriptors)
            0,                                                                  // Base register t0
            0,                                                                  // Register space 0
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE                           // Textures may be loaded/swapped
        );

        rootParameters[DX12_ROOT_PARAM_TEXTURE_TABLE].InitAsDescriptorTable(
            1,                                                                  // 1 descriptor range
            textureRanges,                                                      // Range: t0-t8
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
        );

        // Root Parameter 7: b6 — ShadowBuffer (PCF shadow map data; useShadowMap=0 disables shadow rendering)
        rootParameters[DX12_ROOT_PARAM_SHADOW_BUFFER].InitAsConstantBufferView(
            6,                                                                  // Register b6
            0,                                                                  // Register space 0
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,                           // Updated when shadow map changes
            D3D12_SHADER_VISIBILITY_PIXEL                                       // Pixel shader only
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

        // Static Sampler 2: PCF Shadow Comparison Sampler (s2)
        // MUST be a comparison filter — ModelPShader declares "SamplerComparisonState shadowSampler : register(s2)"
        // and uses SampleCmpLevelZero().  A non-comparison filter here causes E_INVALIDARG at PSO creation.
        staticSamplers[DX12_SAMPLER_ANISOTROPIC] = CD3DX12_STATIC_SAMPLER_DESC(
            2,                                                                  // Shader register s2
            D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,                   // Bilinear PCF comparison filter
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,                                  // Border address mode U (shadow outside map = lit)
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,                                  // Border address mode V
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,                                  // Border address mode W
            0.0f,                                                               // Mip LOD bias
            1,                                                                  // Max anisotropy (unused for comparison)
            D3D12_COMPARISON_FUNC_LESS_EQUAL,                                   // Lit when stored depth <= current depth
            D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,                             // Border = 1.0 (lit) outside shadow frustum
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
        // Resolve the directory that contains the executable so we can locate the
        // pre-compiled CSO files that FXC placed there during the build step.
        wchar_t exePathBuf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
        std::wstring exeDir(exePathBuf);
        auto lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
            exeDir = exeDir.substr(0, lastSlash + 1);

        // Load pre-compiled vertex shader (ModelVShader.cso — compiled by FXC during build)
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

        std::wstring vsPath = exeDir + L"ModelVShader.cso";
        HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), &vertexShader);
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_CRITICAL,
                L"DX12Renderer: Failed to load ModelVShader.cso from: %s", vsPath.c_str());
            ThrowError("Vertex shader load failed (ModelVShader.cso not found)");
            return;
        }

        // Load pre-compiled pixel shader (ModelPShader.cso — compiled by FXC during build)
        std::wstring psPath = exeDir + L"ModelPShader.cso";
        hr = D3DReadFileToBlob(psPath.c_str(), &pixelShader);
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_CRITICAL,
                L"DX12Renderer: Failed to load ModelPShader.cso from: %s", psPath.c_str());
            ThrowError("Pixel shader load failed (ModelPShader.cso not found)");
            return;
        }

        // Define the vertex input layout matching the Model vertex structure.
        // Vertex struct (DX11/DX12): position(12) + normal(12) + texCoord(8) + tangent(12) + tangentW(4) = 48 bytes.
        // TANGENT must be R32G32B32A32_FLOAT (float4) to match the shader's "float4 tangent : TANGENT"
        // and the struct layout of XMFLOAT3 tangent + float tangentW at offset 32.
        // BITANGENT is NOT a vertex buffer input — the shader computes it from N and T.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
            debug.logDebugMessage(LogLevel::LOG_CRITICAL,
                L"DX12Renderer: CreateGraphicsPipelineState failed. HRESULT: 0x%08X", hr);
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
        // Validate HLSL source files exist in Assets/Shaders/ (same path as DX11 pipeline)
        auto vsPath = ShadersDir / L"ModelVertex.hlsl";
        auto psPath = ShadersDir / L"ModelPixel.hlsl";

        if (!std::filesystem::exists(vsPath))
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"DX12Renderer: HLSL source not found at %ls (CSO may still exist in exe dir)", vsPath.c_str());
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            else
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Found HLSL source: %ls", vsPath.c_str());
        #endif

        if (!std::filesystem::exists(psPath))
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"DX12Renderer: HLSL source not found at %ls (CSO may still exist in exe dir)", psPath.c_str());
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            else
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Found HLSL source: %ls", psPath.c_str());
        #endif

        // Pre-compiled CSO files are loaded in CreatePipelineState() via D3DReadFileToBlob

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
            D3D12_RESOURCE_STATE_COMMON,                                        // Upload-heap buffers are always in COMMON
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
            D3D12_RESOURCE_STATE_COMMON,                                        // Upload-heap buffers are always in COMMON
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

        // Create EnvBuffer (b5) — matches ModelPShader's cbuffer EnvBuffer : register(b5)
        // Layout: float envIntensity, float3 envTint, float mipLODBias, float fresnel0, float2 _padEnv  (32 bytes)
        UINT envBufferSize = (32 + 255) & ~255;                                 // 256-byte aligned
        CD3DX12_RESOURCE_DESC envBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(envBufferSize);

        hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &envBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_envBuffer)
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create EnvBuffer (b5).");
            ThrowError("CreateConstantBuffer failed for EnvBuffer");
            return;
        }

        m_envBuffer->SetName(L"DX12Renderer_EnvBuffer");

        // Zero-initialise EnvBuffer with safe defaults
        {
            void* pData = nullptr;
            CD3DX12_RANGE readRange(0, 0);
            if (SUCCEEDED(m_envBuffer->Map(0, &readRange, &pData)))
            {
                memset(pData, 0, envBufferSize);
                // envIntensity = 1.0, fresnel0 = 0.04 (dielectric default)
                float* f = static_cast<float*>(pData);
                f[0] = 1.0f;  // envIntensity
                f[5] = 0.04f; // fresnel0
                m_envBuffer->Unmap(0, nullptr);
            }
        }

        // Create ShadowBuffer (b6) — matches ModelPShader cbuffer ShadowBuffer : register(b6)
        // Layout: float4x4 lightViewProj (64 bytes) + shadowBias + shadowStrength + useShadowMap + shadowMapSize (16 bytes) = 80 bytes
        // useShadowMap is initialised to 0 so shadows are disabled until a shadow map is provided.
        UINT shadowBufferSize = (80 + 255) & ~255;                              // 256-byte aligned
        CD3DX12_RESOURCE_DESC shadowBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shadowBufferSize);

        hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shadowBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_shadowBuffer)
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Failed to create ShadowBuffer (b6).");
            ThrowError("CreateConstantBuffer failed for ShadowBuffer");
            return;
        }

        m_shadowBuffer->SetName(L"DX12Renderer_ShadowBuffer");

        // Zero-initialise — useShadowMap (offset 72) = 0.0f disables shadow sampling in shader
        {
            void* pData = nullptr;
            CD3DX12_RANGE readRange(0, 0);
            if (SUCCEEDED(m_shadowBuffer->Map(0, &readRange, &pData)))
            {
                memset(pData, 0, shadowBufferSize);
                m_shadowBuffer->Unmap(0, nullptr);
            }
        }

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Constant buffers created successfully. Camera CB Size: %d, Light CB Size: %d, Env CB Size: %d, Shadow CB Size: %d",
                constantBufferSize, lightBufferSize, envBufferSize, shadowBufferSize);
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

                // asm MemoryCopy: REP MOVSQ — uploads ConstantBuffer to mapped GPU memory
                MemoryCopy(&cb, pCBData, sizeof(ConstantBuffer));

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

                // Bulk copy all valid lights in one shot — avoids per-element loop overhead.
                // asm MemoryCopy: REP MOVSQ bulk quad-word transfer.
                if (glb.numLights > 0)
                    MemoryCopy(globalLights.data(), glb.lights,
                               sizeof(LightStruct) * static_cast<size_t>(glb.numLights));

                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG_LIGHTING_)
                    for (int i = 0; i < glb.numLights; ++i)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"DX12Renderer: Global Light[%d] active=%d intensity=%.2f color=(%.2f %.2f %.2f) range=%.2f type=%d position=(%.2f, %.2f, %.2f)",
                            i, glb.lights[i].active, glb.lights[i].intensity,
                            glb.lights[i].color.x, glb.lights[i].color.y, glb.lights[i].color.z,
                            glb.lights[i].range, glb.lights[i].type,
                            glb.lights[i].position.x, glb.lights[i].position.y, glb.lights[i].position.z);
                #endif

                // Zero the full allocated GPU region before copying — matches DX11's
                // kGlobalLightCBMinBytes approach to ensure the shader never reads stale data
                // beyond what the CPU struct covers (upload heap is 256-byte aligned).
                // asm MemoryZero: REP STOSQ; asm MemoryCopy: REP MOVSQ.
                static const UINT kGlobalLightCBMinBytes = 1728;
                MemoryZero(pLightData, kGlobalLightCBMinBytes);
                MemoryCopy(&glb, pLightData, sizeof(GlobalLightBuffer));

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
        // Wait until the GPU has finished work for the current frame context.
        // The fence was signaled (and m_frameIndex advanced) by the PREVIOUS frame's
        // MoveToNextFrame() call, so no Signal or index advance is done here.
        if (m_fence->GetCompletedValue() < m_frameContexts[m_frameIndex].fenceValue) {
            HRESULT hr = m_fence->SetEventOnCompletion(m_frameContexts[m_frameIndex].fenceValue, m_fenceEvent);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set fence event.");
                return;
            }

            // Use a shorter timeout during shutdown so the thread is not blocked for 5 s.
            const DWORD timeoutMs = threadManager.threadVars.bIsShuttingDown.load() ? 500 : 5000;
            DWORD waitResult = WaitForSingleObject(m_fenceEvent, timeoutMs);
            if (waitResult != WAIT_OBJECT_0)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Fence wait timed out — forcing frame advance.");

            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Waited for frame %d to complete.", m_frameIndex);
            #endif
        }
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
        // Record the fence value that marks the end of this frame's GPU work.
        m_frameContexts[m_frameIndex].fenceValue = m_fenceValue;

        // Signal the fence so WaitForPreviousFrame() on the NEXT iteration knows
        // when this frame's GPU work is done.
        HRESULT hr = m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to signal fence for next frame.");
            return;
        }

        // Advance to whichever buffer the swap chain will render into next.
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Do NOT wait here — WaitForPreviousFrame() at the start of the next render
        // iteration handles the wait.  A second WaitForSingleObject here causes two
        // GPU-serialisation stalls per frame on a 60 Hz display, halving the FPS to ~30.

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

        // Reset the composite allocator — it was last used FrameCount frames ago, which the fence
        // wait in WaitForPreviousFrame() has already confirmed is complete on the GPU.
        if (m_frameContexts[m_frameIndex].compositeAllocator)
            m_frameContexts[m_frameIndex].compositeAllocator->Reset();

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
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_frameContexts[m_frameIndex].rtvHandle);
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

        if (m_envBuffer) {
            m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_ENVIRONMENT_BUFFER,
                m_envBuffer->GetGPUVirtualAddress());
        }

        if (m_shadowBuffer) {
            m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_SHADOW_BUFFER,
                m_shadowBuffer->GetGPUVirtualAddress());
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
    if (!m_commandQueue || !m_fence || !m_fenceEvent) return;
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

        // Wait until the fence has been processed (5 s max — prevents infinite hang on device loss or shutdown)
        if (m_fence->GetCompletedValue() < fenceValue) {
            hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set fence event in WaitForGPUToFinish.");
                return;
            }

            const DWORD gpuTimeoutMs = threadManager.threadVars.bIsShuttingDown.load() ? 500 : 5000;
            DWORD waitResult = WaitForSingleObject(m_fenceEvent, gpuTimeoutMs);
            if (waitResult != WAIT_OBJECT_0) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: GPU wait timed out or failed in WaitForGPUToFinish — forcing cleanup.");
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

        // Create DirectX 11 on 12 device for interoperability.
        // D3D11On12CreateDevice outputs an ID3D11Device*; we then QI for ID3D11On12Device.
        {
            ComPtr<ID3D11Device> tempD3D11Device;
            ComPtr<ID3D11DeviceContext> tempD3D11Context;
            IUnknown* commandQueues[] = { m_commandQueue.Get() };
            hr = D3D11On12CreateDevice(
                m_d3d12Device.Get(),                                            // DirectX 12 device
                creationFlags,                                                  // Creation flags
                featureLevels,                                                  // Feature levels array
                _countof(featureLevels),                                        // Number of feature levels
                commandQueues,                                                  // Command queue array
                1,                                                              // Number of command queues
                0,                                                              // Node mask
                &tempD3D11Device,                                               // Output ID3D11Device*
                &tempD3D11Context,                                              // Output ID3D11DeviceContext*
                &selectedFeatureLevel                                           // Selected feature level
            );

            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to create DirectX 11 on 12 device.");
                m_dx11Dx12Compat.dx11On12Device = nullptr;
            }
            else {
                // Promote to ID3D11On12Device via QueryInterface
                tempD3D11Device.As(&m_dx11Dx12Compat.dx11On12Device);
                // Keep the wrapped DX11 context for Direct2D interop
                m_dx11Dx12Compat.dx11Context = tempD3D11Context;
            }
        }

        if (m_dx11Dx12Compat.dx11On12Device) {
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

        // Get DXGI device from the DX11On12 device (NOT the standalone dx11Device).
        // The D2D device must share the same underlying device as the DX11On12 wrapper
        // so that D2D bitmaps created from wrapped DX12 back-buffer surfaces are valid.
        ComPtr<IDXGIDevice> dxgiDevice;
        if (m_dx11Dx12Compat.dx11On12Device)
            hr = m_dx11Dx12Compat.dx11On12Device.As(&dxgiDevice);
        else
            hr = m_dx11Dx12Compat.dx11Device.As(&dxgiDevice);   // fallback only
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get DXGI device for Direct2D device creation.");
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

        // Force 96 DPI so all D2D coordinates are in physical pixels, matching iOrigWidth/iOrigHeight.
        // Without this the D2D context inherits the monitor DPI (e.g. 144 at 150% scaling), shifting
        // every GUI element by the DPI ratio and placing the GameMenu panel off-screen.
        m_d2dContext->SetDpi(96.0f, 96.0f);

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
        // Release per-frame D2D targets and wrapped back buffers first
        for (UINT i = 0; i < FrameCount; ++i)
        {
            m_d2dRenderTargets[i].Reset();
            m_wrappedBackBuffers[i].Reset();
            m_d2dOffscreenBitmap[i].Reset();
            m_d2dWrappedOffscreen[i].Reset();
            m_d2dOffscreenTex[i].Reset();
        }

        // Release composite PSO resources
        m_compositePSO.Reset();
        m_compositeRS.Reset();

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
// Create Per-Frame D2D Render Targets
// Must be called after InitializeDX11On12Compatibility() AND after the swap chain
// back buffers exist. Creates one wrapped DX11 resource + one ID2D1Bitmap1 per frame.
//-----------------------------------------
bool DX12Renderer::CreateD2DRenderTargets()
{
    if (!m_dx11Dx12Compat.dx11On12Device || !m_d2dContext || !m_swapChain)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Cannot create D2D targets — missing dx11On12Device, d2dContext or swapChain.");
        return false;
    }

    // Release any existing resources first
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_d2dRenderTargets[i].Reset();
        m_wrappedBackBuffers[i].Reset();
    }

    // Use 96 DPI for D2D render target bitmaps — must match the SetDpi(96,96) applied to
    // m_d2dContext in InitializeDX11On12Compatibility so the coordinate spaces are identical.
    const FLOAT dpiX = 96.0f, dpiY = 96.0f;

    D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX, dpiY);

    for (UINT i = 0; i < m_effectiveFrameCount; ++i)
    {
        // Get the DX12 back buffer for this frame slot
        ComPtr<ID3D12Resource> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: GetBuffer(%u) failed for D2D target creation.", i);
            return false;
        }

        // Wrap as a DX11 resource so DX11On12 / D2D can render into it.
        // SDK 10.0.26100 uses CreateWrappedResource (singular) — wraps one resource at a time.
        // inState  = RENDER_TARGET : DX12 state the resource is in when AcquireWrappedResources() is called.
        // outState = PRESENT       : DX12 state it transitions to when ReleaseWrappedResources() is called.
        D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET };
        hr = m_dx11Dx12Compat.dx11On12Device->CreateWrappedResource(
            backBuffer.Get(),                           // IUnknown* — DX12 resource
            &d3d11Flags,                                // D3D11 bind flags
            D3D12_RESOURCE_STATE_RENDER_TARGET,         // InState
            D3D12_RESOURCE_STATE_PRESENT,               // OutState
            IID_PPV_ARGS(&m_wrappedBackBuffers[i]));    // -> ID3D11Resource
        if (FAILED(hr))
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: CreateWrappedResource(%u) failed (0x%08X).", i, hr);
            return false;
        }

        // QI for the DXGI surface — needed by CreateBitmapFromDxgiSurface
        ComPtr<IDXGISurface> surface;
        hr = m_wrappedBackBuffers[i].As(&surface);
        if (FAILED(hr))
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: QI to IDXGISurface failed for frame %u.", i);
            return false;
        }

        // Create a D2D target bitmap backed by this surface
        hr = m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProps, &m_d2dRenderTargets[i]);
        if (FAILED(hr))
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: CreateBitmapFromDxgiSurface failed for frame %u (0x%08X).", i, hr);
            return false;
        }

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: D2D render target created for frame %u.", i);
        #endif
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: Per-frame D2D render targets created successfully.");
    return true;
}

//-----------------------------------------
// CreateD2DOffscreenTargets
// Creates per-frame off-screen RGBA textures used as D2D render targets instead of the
// swap-chain back buffer.  Eliminating PRESENT-state transitions on the back buffer
// removes the DX11On12 acquisition overhead that was the primary source of the ~40fps cap.
//-----------------------------------------
bool DX12Renderer::CreateD2DOffscreenTargets()
{
    if (!m_dx11Dx12Compat.dx11On12Device || !m_d2dContext || !m_d3d12Device)
        return false;

    for (UINT i = 0; i < m_effectiveFrameCount; ++i) {
        m_d2dOffscreenTex[i].Reset();
        m_d2dWrappedOffscreen[i].Reset();
        m_d2dOffscreenBitmap[i].Reset();
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension            = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width                = static_cast<UINT64>(iOrigWidth);
    texDesc.Height               = static_cast<UINT>(iOrigHeight);
    texDesc.DepthOrArraySize     = 1;
    texDesc.MipLevels            = 1;
    texDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count     = 1;
    texDesc.Flags                = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearVal   = {};
    clearVal.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
    // clearVal.Color = {0,0,0,0} — transparent black default

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    for (UINT i = 0; i < m_effectiveFrameCount; ++i) {
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_SHARED,             // Required for 11on12 CreateWrappedResource to pass debug validation
            &texDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearVal,
            IID_PPV_ARGS(&m_d2dOffscreenTex[i]));
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"DX12Renderer: CreateCommittedResource for D2D off-screen[%u] failed (0x%08X)", i, hr);
            return false;
        }
        m_d2dOffscreenTex[i]->SetName((L"D2DOffscreenTex_" + std::to_wstring(i)).c_str());

        // SRV descriptor in the reserved composite SRV slots
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;

        const UINT srvSlot = DX12_D2D_COMPOSITE_SRV_BASE + i;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuH = m_cbvSrvUavHeap.cpuStart;
        cpuH.ptr += srvSlot * m_cbvSrvUavHeap.handleIncrementSize;
        m_d3d12Device->CreateShaderResourceView(m_d2dOffscreenTex[i].Get(), &srvDesc, cpuH);

        D3D12_GPU_DESCRIPTOR_HANDLE gpuH = m_cbvSrvUavHeap.gpuStart;
        gpuH.ptr += srvSlot * m_cbvSrvUavHeap.handleIncrementSize;
        m_d2dOffscreenSRV[i] = gpuH;

        // Wrap as DX11 resource: InState=RENDER_TARGET (D2D writes here), OutState=SHADER_RESOURCE (composite reads)
        D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE };
        hr = m_dx11Dx12Compat.dx11On12Device->CreateWrappedResource(
            m_d2dOffscreenTex[i].Get(),
            &d3d11Flags,
            D3D12_RESOURCE_STATE_RENDER_TARGET,   // InState
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // OutState
            IID_PPV_ARGS(&m_d2dWrappedOffscreen[i]));
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"DX12Renderer: CreateWrappedResource for D2D off-screen[%u] failed (0x%08X)", i, hr);
            return false;
        }

        ComPtr<IDXGISurface> surface;
        hr = m_d2dWrappedOffscreen[i].As(&surface);
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"DX12Renderer: IDXGISurface QI for D2D off-screen[%u] failed (0x%08X)", i, hr);
            return false;
        }

        hr = m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &bmpProps, &m_d2dOffscreenBitmap[i]);
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"DX12Renderer: CreateBitmapFromDxgiSurface for D2D off-screen[%u] failed (0x%08X)", i, hr);
            return false;
        }
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: D2D off-screen targets created successfully.");
    return true;
}

//-----------------------------------------
// CreateD2DCompositePSO
// Compiles a minimal full-screen quad VS + alpha-blend PS and creates the PSO used to
// composite the D2D off-screen texture onto the back buffer.
//-----------------------------------------
bool DX12Renderer::CreateD2DCompositePSO()
{
    HRESULT hr;

    // Root signature: one descriptor table (t0 SRV) + one static bilinear-clamp sampler (s0)
    D3D12_DESCRIPTOR_RANGE srvRange         = {};
    srvRange.RangeType                      = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                 = 1;
    srvRange.BaseShaderRegister             = 0;
    srvRange.RegisterSpace                  = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParam          = {};
    rootParam.ParameterType                 = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam.DescriptorTable.NumDescriptorRanges = 1;
    rootParam.DescriptorTable.pDescriptorRanges   = &srvRange;
    rootParam.ShaderVisibility              = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler       = {};
    sampler.Filter                          = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU                        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV                        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW                        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister                  = 0;
    sampler.RegisterSpace                   = 0;
    sampler.ShaderVisibility                = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD                          = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc                  = D3D12_COMPARISON_FUNC_NEVER;

    D3D12_ROOT_SIGNATURE_DESC rsDesc        = {};
    rsDesc.NumParameters                    = 1;
    rsDesc.pParameters                      = &rootParam;
    rsDesc.NumStaticSamplers                = 1;
    rsDesc.pStaticSamplers                  = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized, rsError;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &rsError);
    if (FAILED(hr)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR,
            L"DX12Renderer: D3D12SerializeRootSignature (composite) failed (0x%08X)", hr);
        return false;
    }

    hr = m_d3d12Device->CreateRootSignature(0,
        serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&m_compositeRS));
    if (FAILED(hr)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR,
            L"DX12Renderer: CreateRootSignature (composite) failed (0x%08X)", hr);
        return false;
    }
    m_compositeRS->SetName(L"D2DCompositeRS");

    // Full-screen quad VS: struct output gives deterministic register assignment —
    // the inline out-param pattern (void main(...,out float4 p:SV_POSITION,out float2 uv:TEXCOORD0))
    // causes the HLSL compiler to map TEXCOORD0 to a hardware register that does not
    // match the PS input, producing a PSO linkage E_INVALIDARG.
    static const char vsSource[] =
        "struct VSOut{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};"
        "VSOut main(uint id:SV_VertexID){"
        "VSOut o;"
        "o.uv=float2((id&1u)?1.0f:0.0f,(id&2u)?0.0f:1.0f);"
        "o.pos=float4(o.uv.x*2.0f-1.0f,1.0f-o.uv.y*2.0f,0.0f,1.0f);"
        "return o;}";

    // Alpha-blend PS: sample off-screen texture; premultiplied alpha blend set on PSO (ONE/INV_SRC_ALPHA).
    // Must use the same VSOut struct as the VS so D3DCompile assigns matching interpolant registers —
    // a standalone 'float2 uv:TEXCOORD0' parameter can land on a different hardware register.
    static const char psSource[] =
        "struct VSOut{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};"
        "Texture2D<float4> t:register(t0);SamplerState s:register(s0);"
        "float4 main(VSOut i):SV_TARGET{return t.Sample(s,i.uv);}";

    ComPtr<ID3DBlob> vsBlob, psBlob, compErr;
    hr = D3DCompile(vsSource, sizeof(vsSource) - 1, "CompositeVS", nullptr, nullptr,
                    "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &compErr);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: D2D composite VS compile failed");
        return false;
    }

    hr = D3DCompile(psSource, sizeof(psSource) - 1, "CompositePS", nullptr, nullptr,
                    "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &compErr);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: D2D composite PS compile failed");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature          = m_compositeRS.Get();
    psoDesc.VS                      = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                      = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // Premultiplied alpha blend: src=ONE, dst=INV_SRC_ALPHA
    psoDesc.BlendState              = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0].BlendEnable    = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.SampleMask              = UINT_MAX;
    psoDesc.RasterizerState         = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    // No depth/stencil — composite draws over 3D content
    psoDesc.DepthStencilState       = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable    = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;

    psoDesc.InputLayout             = {};                               // SV_VertexID — no VB
    psoDesc.PrimitiveTopologyType   = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets        = 1;
    psoDesc.RTVFormats[0]           = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat               = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count        = 1;

    hr = m_d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_compositePSO));
    if (FAILED(hr)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR,
            L"DX12Renderer: CreateGraphicsPipelineState (composite) failed (0x%08X)", hr);
#ifdef _DEBUG
        DrainInfoQueue();   // Log the D3D12 validation detail that explains why it failed
#endif
        return false;
    }
    m_compositePSO->SetName(L"D2DCompositePSO");

    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: D2D composite PSO created successfully.");
    return true;
}

//-----------------------------------------
// CompositeD2DToBackBuffer
// Records a premultiplied-alpha full-screen quad draw on the currently open command list,
// blending the current frame's D2D off-screen texture over the back buffer.
// Caller must: have already transitioned the back buffer to RENDER_TARGET and
//              called OMSetRenderTargets; restore the main root-signature + heaps afterwards.
//-----------------------------------------
void DX12Renderer::CompositeD2DToBackBuffer(
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    const D3D12_VIEWPORT&       vp,
    const D3D12_RECT&           scissor)
{
    if (!m_compositePSO || !m_compositeRS || !m_d2dOffscreenTex[m_frameIndex])
        return;

    m_commandList->SetGraphicsRootSignature(m_compositeRS.Get());
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvUavHeap.heap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);  // no DSV — depth disabled in PSO
    m_commandList->SetGraphicsRootDescriptorTable(0, m_d2dOffscreenSRV[m_frameIndex]);
    m_commandList->SetPipelineState(m_compositePSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->DrawInstanced(4, 1, 0, 0);
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
        m_renderTargetWidth  = iOrigWidth;
        m_renderTargetHeight = iOrigHeight;

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

        // Pre-populate cbvSrvUavHeap slots 0–8 with null SRVs so that any draw
        // that does not supply per-model textures can point the descriptor table
        // at heap slot 0 and receive a safe (black/zero) sample instead of UB.
        // The descriptor table now covers t0-t8 (9 slots); all 9 are initialised here.
        // Slots 10+ are used by 3D scene textures (see UploadTextureData).
        if (m_d3d12Device && m_cbvSrvUavHeap.heap) {
            D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
            nullSrv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
            nullSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullSrv.Texture2D.MipLevels     = 1;
            for (UINT s = 0; s < 9; ++s) {
                auto cpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(
                    m_cbvSrvUavHeap.cpuStart, s, m_cbvSrvUavHeap.handleIncrementSize);
                m_d3d12Device->CreateShaderResourceView(nullptr, &nullSrv, cpu);
            }
            m_nullTextureGPUHandle = m_cbvSrvUavHeap.gpuStart;  // slots 0-8 are the 9 null SRVs
            // Initialise allocation cursor to the first per-model texture slot so that
            // Model::SetupModelForRendering() can allocate 9 consecutive SRV slots from here.
            m_cbvSrvUavHeap.currentOffset = DX12_MODEL_TEXTURE_HEAP_BASE;
        }

        // Initialize DirectX 11-12 compatibility layer for 2D rendering
        bool compatibilitySuccess = InitializeDX11On12Compatibility();
        if (!compatibilitySuccess) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: DirectX 11-12 compatibility layer failed to initialize. 2D rendering may be limited.");
        }
        else {
            // Create per-frame D2D render targets (wrapped back buffers + D2D bitmaps)
            if (!CreateD2DRenderTargets())
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to create per-frame D2D render targets. 2D images will not be visible.");

            // Create off-screen D2D targets + composite PSO (eliminates PRESENT-state back-buffer transitions)
            if (!CreateD2DOffscreenTargets())
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to create D2D off-screen targets; falling back to direct back-buffer path.");
            else if (!CreateD2DCompositePSO())
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to create D2D composite PSO; falling back to direct back-buffer path.");
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

    // Signal shutdown immediately so fence waits use the short 500ms timeout
    // and the render thread loop's !bIsShuttingDown exit condition fires.
    threadManager.threadVars.bIsShuttingDown.store(true);

    try {
        // Synchronize Thread Closures
        threadManager.StopThread(THREAD_LOADER);
        threadManager.TerminateThread(THREAD_LOADER);

        #ifdef RENDERER_IS_THREAD
            // Wait for the current render operation to finish (2 s max — avoids infinite hang)
            {
                auto waitStart = std::chrono::steady_clock::now();
                while (threadManager.threadVars.bIsRendering.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if (std::chrono::duration<float>(std::chrono::steady_clock::now() - waitStart).count() > 2.0f)
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: bIsRendering still set after 2s — forcing cleanup.");
                        threadManager.threadVars.bIsRendering.store(false);
                        break;
                    }
                }
            }

            // Now terminate the Renderer Thread.
            threadManager.StopThread(THREAD_RENDERER);
            threadManager.TerminateThread(THREAD_RENDERER);
        #endif

        // Wait for GPU to finish all operations before cleanup
        if (m_d3d12Device && m_commandQueue && m_fence) {
            WaitForGPUToFinish();
        }

        // Release model / scene D3D11 resources (vertex buffers, constant buffers, SRVs)
        // BEFORE CleanupDX11On12Compatibility() destroys the 11on12 device they were
        // created on.  Destroying the device first leaves dangling references that cause
        // hangs or crashes on exit from SCENE_GAMETITLE.
        for (int i = 0; i < MAX_MODELS; ++i)
            models[i].DestroyModel();

        scene.CleanUp();

        // Release cached D2D resources before tearing down the device
        m_generalBrush.Reset();
        m_pixelBrush.Reset();
        m_textFormatCache.clear();

        // Clean up DirectX 11-12 compatibility layer (and D2D)
        m_d2dVideoBitmap.Reset();
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

        if (m_envBuffer) {
            m_envBuffer.Reset();
        }

        if (m_shadowBuffer) {
            m_shadowBuffer.Reset();
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

        // DXGI mandates SetFullscreenState(FALSE) before swap chain release when in exclusive fullscreen.
        // Skipping this leaves the display locked at the exclusive resolution until the next reboot.
        if (m_isExclusiveFullscreen && m_swapChain) {
            m_swapChain->SetFullscreenState(FALSE, nullptr);
            m_isExclusiveFullscreen = false;
            if (m_originalDesktopMode.dmSize > 0)
                ChangeDisplaySettingsEx(nullptr, &m_originalDesktopMode, nullptr, 0, nullptr);
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

            // Create Direct2D bitmap from WIC bitmap.
            // D2D1_FACTORY_TYPE_MULTI_THREADED serialises all factory-derived
            // calls internally, so no external lock is needed here.
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
            // For 3D textures: branch on file extension — WIC path for PNG/JPG/BMP,
            // DDS path for block-compressed DDS files.
            std::wstring ext = filename;
            size_t dotPos = ext.rfind(L'.');
            if (dotPos != std::wstring::npos)
                for (auto& c : ext) c = towlower(c);
            bool isDDS = (filename.size() >= 4 &&
                          filename.rfind(L".dds") == filename.size() - 4) ||
                         (filename.size() >= 4 &&
                          filename.rfind(L".DDS") == filename.size() - 4);

            // ── WIC path (PNG / JPG / BMP) ────────────────────────────────────────
            if (!isDDS)
            {
                if (!m_d3d12Device) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: D3D12 device not ready for 3D texture load.");
                    return false;
                }

                // Decode image pixels via WIC → BGRA32
                ComPtr<IWICImagingFactory> wicFactory;
                HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WIC factory creation failed for 3D texture.");
                    return false;
                }

                ComPtr<IWICBitmapDecoder> decoder;
                hr = wicFactory->CreateDecoderFromFilename(filename.c_str(), nullptr,
                    GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
                if (FAILED(hr)) {
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WIC decoder failed for 3D texture: %s", filename.c_str());
                    return false;
                }

                ComPtr<IWICBitmapFrameDecode> frame;
                hr = decoder->GetFrame(0, &frame);
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WIC GetFrame failed for 3D texture.");
                    return false;
                }

                ComPtr<IWICFormatConverter> converter;
                hr = wicFactory->CreateFormatConverter(&converter);
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WIC format converter creation failed.");
                    return false;
                }

                hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WIC converter Initialize failed for 3D texture.");
                    return false;
                }

                UINT texW = 0, texH = 0;
                converter->GetSize(&texW, &texH);
                if (texW == 0 || texH == 0) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: 3D texture has zero dimensions.");
                    return false;
                }

                // Copy BGRA pixels to CPU buffer
                const UINT srcRowPitch = texW * 4;
                std::vector<BYTE> pixels(static_cast<size_t>(srcRowPitch) * texH);
                hr = converter->CopyPixels(nullptr, srcRowPitch, static_cast<UINT>(pixels.size()), pixels.data());
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WIC CopyPixels failed for 3D texture.");
                    return false;
                }

                // Create committed resource on DEFAULT heap (BGRA8, 1 mip, COPY_DEST)
                D3D12_RESOURCE_DESC texDesc = {};
                texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                texDesc.Width              = texW;
                texDesc.Height             = texH;
                texDesc.DepthOrArraySize   = 1;
                texDesc.MipLevels          = 1;
                texDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
                texDesc.SampleDesc.Count   = 1;
                texDesc.SampleDesc.Quality = 0;
                texDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                texDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

                CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
                hr = m_d3d12Device->CreateCommittedResource(
                    &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&m_d3d12Textures[textureIndex]));
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create 3D texture resource (WIC path).");
                    return false;
                }

                std::wstring resName = L"DX12Renderer_3DTexture_" + std::to_wstring(textureIndex);
                m_d3d12Textures[textureIndex]->SetName(resName.c_str());

                // Upload pixel data to GPU
                if (!UploadTextureData(textureIndex, pixels.data(), pixels.size(),
                                       texW, texH, DXGI_FORMAT_B8G8R8A8_UNORM))
                {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: UploadTextureData failed for 3D texture (WIC path).");
                    m_d3d12Textures[textureIndex].Reset();
                    return false;
                }

                // Create SRV in the CBV/SRV/UAV heap (slots 10..10+MAX_TEXTURE_BUFFERS_3D-1)
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                        = DXGI_FORMAT_B8G8R8A8_UNORM;
                srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels           = 1;
                srvDesc.Texture2D.MostDetailedMip     = 0;
                srvDesc.Texture2D.PlaneSlice          = 0;
                srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

                CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
                    m_cbvSrvUavHeap.cpuStart,
                    10 + textureIndex,
                    m_cbvSrvUavHeap.handleIncrementSize);

                m_d3d12Device->CreateShaderResourceView(
                    m_d3d12Textures[textureIndex].Get(), &srvDesc, srvHandle);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"DX12Renderer: 3D texture %d loaded via WIC. Size: %dx%d Format: BGRA8",
                    textureIndex, texW, texH);
#endif
            }
            // ── DDS path (block-compressed DDS) ──────────────────────────────────
            else
            {
                if (!m_d3d12Device) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: D3D12 device not ready for DDS texture load.");
                    return false;
                }

                HANDLE file = CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to open DDS file: %s", filename.c_str());
                    return false;
                }

                LARGE_INTEGER fileSize;
                if (!GetFileSizeEx(file, &fileSize)) {
                    CloseHandle(file);
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get DDS file size.");
                    return false;
                }

                std::vector<BYTE> fileData(fileSize.LowPart);
                DWORD bytesRead = 0;
                if (!ReadFile(file, fileData.data(), fileSize.LowPart, &bytesRead, nullptr)
                    || bytesRead != fileSize.LowPart)
                {
                    CloseHandle(file);
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to read DDS file data.");
                    return false;
                }
                CloseHandle(file);

                if (fileData.size() < sizeof(DWORD) + sizeof(DDS_HEADER)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: DDS file is too small.");
                    return false;
                }

                DWORD magic = *reinterpret_cast<DWORD*>(fileData.data());
                if (magic != MAKEFOURCC('D', 'D', 'S', ' ')) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Invalid DDS magic number.");
                    return false;
                }

                DDS_HEADER* header = reinterpret_cast<DDS_HEADER*>(fileData.data() + sizeof(DWORD));

                DXGI_FORMAT ddsFormat = DXGI_FORMAT_UNKNOWN;
                if      (header->ddspf.dwFourCC == MAKEFOURCC('D','X','T','1')) ddsFormat = DXGI_FORMAT_BC1_UNORM;
                else if (header->ddspf.dwFourCC == MAKEFOURCC('D','X','T','3')) ddsFormat = DXGI_FORMAT_BC2_UNORM;
                else if (header->ddspf.dwFourCC == MAKEFOURCC('D','X','T','5')) ddsFormat = DXGI_FORMAT_BC3_UNORM;
                else {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Unsupported DDS FourCC format.");
                    return false;
                }

                UINT texW    = header->dwWidth;
                UINT texH    = header->dwHeight;
                UINT mips    = header->dwMipMapCount ? header->dwMipMapCount : 1;

                D3D12_RESOURCE_DESC texDesc = {};
                texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                texDesc.Width              = texW;
                texDesc.Height             = texH;
                texDesc.DepthOrArraySize   = 1;
                texDesc.MipLevels          = mips;
                texDesc.Format             = ddsFormat;
                texDesc.SampleDesc.Count   = 1;
                texDesc.SampleDesc.Quality = 0;
                texDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                texDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

                CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
                HRESULT hr = m_d3d12Device->CreateCommittedResource(
                    &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&m_d3d12Textures[textureIndex]));
                if (FAILED(hr)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create 3D DDS texture resource.");
                    return false;
                }

                std::wstring resName = L"DX12Renderer_3DTexture_" + std::to_wstring(textureIndex);
                m_d3d12Textures[textureIndex]->SetName(resName.c_str());

                // Upload compressed pixel data (starts after DDS magic + header)
                const BYTE* pixelStart = fileData.data() + sizeof(DWORD) + sizeof(DDS_HEADER);
                size_t      pixelSize  = fileData.size()  - sizeof(DWORD) - sizeof(DDS_HEADER);

                if (!UploadTextureData(textureIndex, pixelStart, pixelSize, texW, texH, ddsFormat))
                {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: UploadTextureData failed for DDS texture.");
                    m_d3d12Textures[textureIndex].Reset();
                    return false;
                }

                // Create SRV in the CBV/SRV/UAV heap (slots 10..10+MAX_TEXTURE_BUFFERS_3D-1)
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                        = ddsFormat;
                srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels           = mips;
                srvDesc.Texture2D.MostDetailedMip     = 0;
                srvDesc.Texture2D.PlaneSlice          = 0;
                srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

                CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
                    m_cbvSrvUavHeap.cpuStart,
                    10 + textureIndex,
                    m_cbvSrvUavHeap.handleIncrementSize);

                m_d3d12Device->CreateShaderResourceView(
                    m_d3d12Textures[textureIndex].Get(), &srvDesc, srvHandle);

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"DX12Renderer: 3D DDS texture %d loaded. Size: %dx%d Mips: %d Format: %d",
                    textureIndex, texW, texH, mips, ddsFormat);
#endif
            }
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
// Blit 2D Object To Size With Alpha
//-----------------------------------------
void DX12Renderer::Blit2DObjectToSizeWithAlpha(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float alpha)
{
    try {
        if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS)
            return;

        if (!IsDX11CompatibilityAvailable() || !m_d2dContext)
            return;

        if (!m_d2dTextures[int(iIndex)])
            return;

        D2D1_SIZE_F bitmapSize = m_d2dTextures[int(iIndex)]->GetSize();

        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iX),
            static_cast<float>(iY),
            static_cast<float>(iX + iWidth),
            static_cast<float>(iY + iHeight)
        );

        D2D1_RECT_F srcRect = D2D1::RectF(0.0f, 0.0f, bitmapSize.width, bitmapSize.height);

        float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
        m_d2dContext->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),
            destRect,
            clampedAlpha,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            srcRect
        );
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Blit2DObjectToSizeWithAlpha: %s", errorMsg.c_str());
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
// Blit 2D Object with Centered Zoom Crop
//-----------------------------------------
void DX12Renderer::Blit2DCenteredZoom(BlitObj2DIndexType iIndex, int iDestX, int iDestY, int iDestW, int iDestH, float zoomFactor) {
    try {
        if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) return;
        if (!IsDX11CompatibilityAvailable() || !m_d2dContext) return;
        if (!m_d2dTextures[int(iIndex)]) return;

        // Clamp zoom factor to valid range (0.0–0.75)
        float z = std::clamp(zoomFactor, 0.0f, 0.75f);

        D2D1_SIZE_F sz = m_d2dTextures[int(iIndex)]->GetSize();
        float srcW  = sz.width  * (1.0f - z);                                  // Cropped source width
        float srcH  = sz.height * (1.0f - z);                                  // Cropped source height
        float srcX  = (sz.width  - srcW) * 0.5f;                              // Centre-aligned source X
        float srcY  = (sz.height - srcH) * 0.5f;                              // Centre-aligned source Y

        D2D1_RECT_F srcRect  = D2D1::RectF(srcX, srcY, srcX + srcW, srcY + srcH);
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iDestX),
            static_cast<float>(iDestY),
            static_cast<float>(iDestX + iDestW),
            static_cast<float>(iDestY + iDestH)
        );

        m_d2dContext->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),
            destRect, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            srcRect
        );
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Exception in Blit2DCenteredZoom: %s", errorMsg.c_str());
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

        // m_pixelBrush is a member reset in Resize()/Cleanup(), so it is always bound
        // to the current D2D device — no cross-device aliasing risk (unlike a static local).
        if (!m_pixelBrush)
        {
            HRESULT hr = m_d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(color.x, color.y, color.z, color.w),               // RGBA color
                &m_pixelBrush                                                   // Output brush
            );
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create solid color brush for pixel.");
                return;
            }
        }
        else
        {
            // Update existing brush color
            m_pixelBrush->SetColor(D2D1::ColorF(color.x, color.y, color.z, color.w));
        }

        // Define pixel rectangle
        D2D1_RECT_F pixelRect = D2D1::RectF(
            static_cast<FLOAT>(x),                                              // Left
            static_cast<FLOAT>(y),                                              // Top
            static_cast<FLOAT>(x) + pixelSize,                                  // Right (left + size)
            static_cast<FLOAT>(y) + pixelSize                                   // Bottom (top + size)
        );

        // Fill the pixel rectangle
        m_d2dContext->FillRectangle(&pixelRect, m_pixelBrush.Get());

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

            DirectX::XMFLOAT4 convColor = ConvertColor(color.r, color.g, color.b, color.a);
            if (!SetGeneralBrushColor(convColor.x, convColor.y, convColor.z, convColor.w)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set brush color for 2D rectangle.");
                return;
            }

            D2D1_RECT_F rect = D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y);
            m_d2dContext->FillRectangle(&rect, m_generalBrush.Get());

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
// Cached TextFormat helper — creates once per (fontName, size, weight, style) key
//-----------------------------------------
IDWriteTextFormat* DX12Renderer::GetOrCreateTextFormat(const wchar_t* fontName, float fontSize,
    DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style)
{
    if (!m_dwriteFactory) return nullptr;

    std::wstring key = std::wstring(fontName ? fontName : L"") +
        L"|" + std::to_wstring(static_cast<int>(fontSize * 10)) +
        L"|" + std::to_wstring(static_cast<int>(weight)) +
        L"|" + std::to_wstring(static_cast<int>(style));

    auto it = m_textFormatCache.find(key);
    if (it != m_textFormatCache.end() && it->second)
        return it->second.Get();

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        fontName ? fontName : L"Arial", nullptr,
        weight, style, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &fmt);
    if (FAILED(hr)) return nullptr;

    m_textFormatCache[key] = fmt;
    return m_textFormatCache[key].Get();
}

//-----------------------------------------
// Cached Brush helper — creates once, sets color on every call
//-----------------------------------------
bool DX12Renderer::SetGeneralBrushColor(float r, float g, float b, float a)
{
    if (!m_d2dContext) return false;
    if (!m_generalBrush)
    {
        HRESULT hr = m_d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(r, g, b, a), &m_generalBrush);
        if (FAILED(hr)) return false;
    }
    m_generalBrush->SetColor(D2D1::ColorF(r, g, b, a));
    return true;
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

        IDWriteTextFormat* textFormat = GetOrCreateTextFormat(FontName, FontSize);
        if (!textFormat) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get/create text format.");
            return;
        }

        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        if (!SetGeneralBrushColor(r, g, b, a)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set text brush color.");
            return;
        }

        D2D1_RECT_F destRect = D2D1::RectF(position.x, position.y, position.x + 1000.0f, position.y + 200.0f);

        m_d2dContext->DrawText(
            text.c_str(), static_cast<UINT32>(text.size()),
            textFormat, destRect, m_generalBrush.Get());

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

        IDWriteTextFormat* textFormat = GetOrCreateTextFormat(FontName, FontSize);
        if (!textFormat) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get/create text format for sized text.");
            return;
        }

        // The format cache is shared with DrawMyTextCentered which mutates it to
        // CENTER alignment.  Always reset here so label text starts at the left.
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        if (!SetGeneralBrushColor(r, g, b, a)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set brush color for sized text.");
            return;
        }

        D2D1_RECT_F textRect = D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y);

        m_d2dContext->DrawText(
            text.c_str(), static_cast<UINT32>(text.size()),
            textFormat, textRect, m_generalBrush.Get());

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

        IDWriteTextFormat* textFormat = GetOrCreateTextFormat(fontName.c_str(), FontSize);
        if (!textFormat) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get/create text format with font: %s", fontName.c_str());
            return;
        }

        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        if (!SetGeneralBrushColor(r, g, b, a)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set brush color for custom font text.");
            return;
        }

        D2D1_RECT_F destRect = D2D1::RectF(position.x, position.y, position.x + 1000.0f, position.y + 200.0f);

        m_d2dContext->DrawText(
            text.c_str(), static_cast<UINT32>(text.size()),
            textFormat, destRect, m_generalBrush.Get());

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

void DX12Renderer::DrawCircle(const Vector2& center, float radius, const MyColor& color, bool filled) {
    if (!m_d2dContext) return;
    float fr = color.r / 255.0f, fg = color.g / 255.0f, fb = color.b / 255.0f, fa = color.a / 255.0f;
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(fr, fg, fb, fa), &brush);
    if (!brush) return;
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radius, radius);
    if (filled) m_d2dContext->FillEllipse(ellipse, brush.Get());
    else        m_d2dContext->DrawEllipse(ellipse, brush.Get());
}

//-----------------------------------------
// 2D Clip Rect (D2D PushAxisAlignedClip)
//-----------------------------------------
void DX12Renderer::PushClipRect(float x, float y, float w, float h) {
    if (!m_d2dContext) return;
    m_d2dContext->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h),
                                      D2D1_ANTIALIAS_MODE_ALIASED);
}

void DX12Renderer::PopClipRect() {
    if (!m_d2dContext) return;
    m_d2dContext->PopAxisAlignedClip();
}

//-----------------------------------------
// Draw Centered Text for DirectX 12
//-----------------------------------------
void DX12Renderer::DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight, bool bold) {
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

        DWRITE_FONT_WEIGHT weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        IDWriteTextFormat* textFormat = GetOrCreateTextFormat(FontName, FontSize, weight);
        if (!textFormat) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get/create text format for centered text.");
            return;
        }
        // Use leading/near alignment — centering is computed from exact glyph metrics below.
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        float r = static_cast<float>(color.r) / 255.0f;
        float g = static_cast<float>(color.g) / 255.0f;
        float b = static_cast<float>(color.b) / 255.0f;
        float a = static_cast<float>(color.a) / 255.0f;

        if (!SetGeneralBrushColor(r, g, b, a)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to set brush color for centered text.");
            return;
        }

        // Create a temporary text layout to measure exact glyph extents for pixel-perfect centering.
        ComPtr<IDWriteTextLayout> textLayout;
        HRESULT hr = m_dwriteFactory->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()),
            textFormat, 10000.f, 1000.f, &textLayout);
        if (FAILED(hr) || !textLayout) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to create text layout for centered text.");
            return;
        }

        DWRITE_TEXT_METRICS metrics{};
        textLayout->GetMetrics(&metrics);

        // Pixel-perfect centering: offset by half control size minus half glyph size.
        float centredX = position.x + (controlWidth  * 0.5f) - (metrics.width  * 0.5f);
        float centredY = position.y + (controlHeight * 0.5f) - (metrics.height * 0.5f);

        m_d2dContext->DrawText(
            text.c_str(), static_cast<UINT32>(text.size()),
            textFormat,
            D2D1::RectF(centredX, centredY, centredX + metrics.width, centredY + metrics.height),
            m_generalBrush.Get());

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

        // Load all 3D textures (PNG / JPG / DDS from tex3DFilename[])
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; i++)
        {
            auto fileName = AssetsDir / tex3DFilename[i];

            if (!LoadTexture(i, fileName, false))
            {
                std::wstring msg = L"DX12Renderer: Failed to load 3D Texture: " + fileName.wstring();
                debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
                texturesFailed++;
                result = false;
            }
            else
            {
                texturesLoaded++;
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                std::wstring msg = L"DX12Renderer: Successfully loaded 3D Texture: " + fileName.wstring();
                debug.logLevelMessage(LogLevel::LOG_DEBUG, msg);
#endif
            }
        }

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
// CreateVideoD2DTexture — allocate a D2D bitmap slot for per-frame video playback.
// Returns the texture index in m_d2dTextures that MoviePlayer should use.
//-----------------------------------------
// Sentinel used by MoviePlayer to identify the dedicated video bitmap.
// Must be outside the normal m_d2dTextures[] range so LoadAllKnownTextures()
// can never fill it.
static constexpr int kVideoBitmapSentinel = MAX_TEXTURE_BUFFERS;

bool DX12Renderer::CreateVideoD2DTexture(int& outTextureIndex, UINT width, UINT height)
{
    outTextureIndex = -1;
    if (!m_d2dContext || !IsDX11CompatibilityAvailable()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: D2D context not ready for video texture creation.");
        return false;
    }
    if (width == 0 || height == 0) return false;

    // Use the dedicated video bitmap rather than a slot inside m_d2dTextures[],
    // which is fully occupied by UI textures after LoadAllKnownTextures().
    m_d2dVideoBitmap.Reset();

    D2D1_SIZE_U bmpSize = D2D1::SizeU(width, height);
    D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    HRESULT hr = m_d2dContext->CreateBitmap(bmpSize, nullptr, 0, bitmapProps, &m_d2dVideoBitmap);
    if (FAILED(hr)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: CreateBitmap for video failed (0x%08X).", hr);
        return false;
    }
    outTextureIndex = kVideoBitmapSentinel;
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Video D2D bitmap created (%ux%u), sentinel=%d.", width, height, kVideoBitmapSentinel);
#endif
    return true;
}

//-----------------------------------------
// UpdateVideoD2DTexture — upload one decoded video frame (BGRA) to the D2D bitmap.
//-----------------------------------------
bool DX12Renderer::UpdateVideoD2DTexture(int textureIndex, const BYTE* pData, UINT rowPitch)
{
    if (!pData) return false;
    ID2D1Bitmap* bmp = (textureIndex == kVideoBitmapSentinel) ? m_d2dVideoBitmap.Get()
                       : (textureIndex >= 0 && textureIndex < MAX_TEXTURE_BUFFERS) ? m_d2dTextures[textureIndex].Get()
                       : nullptr;
    if (!bmp) return false;

    HRESULT hr = bmp->CopyFromMemory(nullptr, pData, rowPitch);
    if (FAILED(hr)) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"DX12Renderer: CopyFromMemory for video failed (0x%08X).", hr);
        return false;
    }
    return true;
}

//-----------------------------------------
// ReleaseVideoD2DTexture — free the video D2D bitmap.
//-----------------------------------------
void DX12Renderer::ReleaseVideoD2DTexture(int textureIndex)
{
    if (textureIndex == kVideoBitmapSentinel)
        m_d2dVideoBitmap.Reset();
    else if (textureIndex >= 0 && textureIndex < MAX_TEXTURE_BUFFERS)
        m_d2dTextures[textureIndex].Reset();
}

//-----------------------------------------
// BlitVideoBitmap — draw m_d2dVideoBitmap; called from MoviePlayer::Render()
// inside the render frame's active BeginDraw/EndDraw context.
//-----------------------------------------
void DX12Renderer::BlitVideoBitmap(float x, float y, float w, float h)
{
    if (!m_d2dContext || !m_d2dVideoBitmap) return;
    D2D1_RECT_F dest = D2D1::RectF(x, y, x + w, y + h);
    m_d2dContext->DrawBitmap(m_d2dVideoBitmap.Get(), dest, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
}

//-----------------------------------------
// Resize DirectX 12 Renderer
//-----------------------------------------
bool DX12Renderer::Resize(uint32_t width, uint32_t height)
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"DX12Renderer: Resize requested: %dx%d", width, height);
#endif

    std::string lockName = "dx12_renderer_resize_lock";

    // Try to acquire the render mutex with a 1000ms timeout
    if (!threadManager.TryLock(lockName, 1000)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Could not acquire render mutex for resize operation - timeout reached");
        return false;
    }

    try {
        if (!m_swapChain || !m_d3d12Device || !m_commandQueue)
        {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX12Renderer: Missing critical DirectX 12 interfaces for resize.");
            threadManager.RemoveLock(lockName);
            return false;
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

        // Release D2D per-frame bitmaps and 11on12 wrapped back buffers FIRST.
        // Both hold COM references to the swap-chain back-buffer resources.
        // ResizeBuffers returns DXGI_ERROR_INVALID_CALL if any outstanding reference
        // to a back-buffer remains — these two vectors are the ones that persist
        // across frames and are not released by the loop below.
        for (UINT i = 0; i < FrameCount; ++i) {
            m_d2dRenderTargets[i].Reset();
            m_wrappedBackBuffers[i].Reset();
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
            m_effectiveFrameCount,                                              // Buffer count (3=triple/2=double)
            width,                                                              // New width
            height,                                                             // New height
            DXGI_FORMAT_R8G8B8A8_UNORM,                                         // Format
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH                              // Allow mode switching
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to resize swap chain buffers.");
            threadManager.RemoveLock(lockName);
            return false;
        }

        // Recreate render target views for active frames
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.cpuStart);
        for (UINT i = 0; i < m_effectiveFrameCount; ++i) {
            // Get the back buffer resource from the swap chain
            hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_frameContexts[i].renderTarget));
            if (FAILED(hr)) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: Failed to get swap chain buffer %d after resize.", i);
                threadManager.RemoveLock(lockName);
                return false;
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
            return false;
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
        m_renderTargetWidth  = width;
        m_renderTargetHeight = height;

        // Recreate per-frame D2D render targets for the new back buffers.
        // The D2D factory, device, context, and 11on12 device are NOT tied to the
        // swap-chain back-buffer resources — only m_wrappedBackBuffers[] and
        // m_d2dRenderTargets[] are, and both were reset above before ResizeBuffers().
        // Tearing down and recreating the full D2D stack (CleanupDX11On12Compatibility
        // + InitializeDX11On12Compatibility) on every resize is unnecessary and
        // introduces a window where the render thread can access released D2D state.
        // CreateD2DRenderTargets() wraps the new back-buffer resources and creates
        // new per-frame D2D bitmaps — that is all a resize requires.
        if (m_d2dContext && m_dx11Dx12Compat.dx11On12Device) {
            if (!CreateD2DRenderTargets())
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to recreate D2D render targets after resize.");
            if (!CreateD2DOffscreenTargets())
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: Failed to recreate D2D off-screen targets after resize.");
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
        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12Renderer: Exception in Resize: %s", errorMsg.c_str());

        // Ensure lock is released on exception
        threadManager.RemoveLock(lockName);
        return false;
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

        // Get the output (monitor) information — fall back to adapter on early startup failure
        ComPtr<IDXGIOutput> output;
        HRESULT hr = m_swapChain->GetContainingOutput(&output);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: GetContainingOutput failed, trying adapter fallback");
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(m_dx11Dx12Compat.dx11Device.As(&dxgiDevice)) &&
                SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
            {
                adapter->EnumOutputs(0, &output);
            }
            if (!output) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: No output found — fullscreen skipped");
                bFullScreenTransition.store(false);
                threadManager.threadVars.bSettingFullScreen.store(false);
                return false;
            }
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

        // Capture the current desktop display mode before any mode change so it can be restored on exit
        m_originalDesktopMode = {};
        m_originalDesktopMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &m_originalDesktopMode);

        // Save current window size before going to exclusive fullscreen
        RECT rc;
        GetClientRect(hwnd, &rc);
        prevWindowedWidth = rc.right - rc.left;
        prevWindowedHeight = rc.bottom - rc.top;

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12Renderer: Saved windowed size: %dx%d", prevWindowedWidth, prevWindowedHeight);
#endif

        // Get the output (monitor) that contains the swap chain's window.
        // GetContainingOutput can fail early in startup before the window is fully
        // associated with a display — fall back to the adapter's first output.
        ComPtr<IDXGIOutput> output;
        HRESULT hr = m_swapChain->GetContainingOutput(&output);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: GetContainingOutput failed, trying adapter fallback");
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(m_dx11Dx12Compat.dx11Device.As(&dxgiDevice)) &&
                SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
            {
                adapter->EnumOutputs(0, &output);
            }
            if (!output) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: No display output found — fullscreen skipped");
                bFullScreenTransition.store(false);
                threadManager.threadVars.bSettingFullScreen.store(false);
                return false;
            }
        }

        // Enumerate available display modes to verify the requested resolution is supported
        UINT numModes = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

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
        targetMode.RefreshRate.Numerator = static_cast<UINT>(config.myConfig.refreshRate);
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

        m_isExclusiveFullscreen = true;

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
    // Resources have already been released — nothing to do.
    if (bHasCleanedUp) return false;

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
        m_isExclusiveFullscreen = false;

        // If we are shutting down, we do not need to worry about resizing buffers
        if (threadManager.threadVars.bIsShuttingDown.load()) {
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return true;
        }

        // Get appropriate window dimensions from stored values
        UINT windowedWidth = prevWindowedWidth > 0 ? prevWindowedWidth : static_cast<UINT>(config.myConfig.resolutionWidth);
        UINT windowedHeight = prevWindowedHeight > 0 ? prevWindowedHeight : static_cast<UINT>(config.myConfig.resolutionHeight);

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
            D3D12_RESOURCE_STATE_COMMON,                                        // Upload-heap buffers are always in COMMON
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
                    std::min(srcRowPitch, static_cast<UINT>(rowSizeInBytes))   // Copy size (minimum of source and destination)
                );
            }
        }
        else {
            // Block compressed format - copy entire data block
            memcpy(dstData, srcData, std::min(dataSize, static_cast<size_t>(uploadBufferSize)));
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

        // Resource was created in COPY_DEST by the caller — no barrier needed before the copy.
        // Copy data from upload buffer to texture
        CD3DX12_TEXTURE_COPY_LOCATION dstLocation(m_d3d12Textures[textureIndex].Get(), 0);
        CD3DX12_TEXTURE_COPY_LOCATION srcLocation(uploadBuffer.Get(), layout);
        uploadCommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

        // Transition to PIXEL_SHADER_RESOURCE so the render pipeline can sample it
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

        // Wait for upload to complete using a dedicated per-upload fence so we
        // do not corrupt the render frame's m_fenceValue / m_fenceEvent.
        {
            ComPtr<ID3D12Fence> uploadFence;
            hr = m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));
            if (SUCCEEDED(hr))
            {
                HANDLE uploadEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (uploadEvent)
                {
                    m_commandQueue->Signal(uploadFence.Get(), 1);
                    uploadFence->SetEventOnCompletion(1, uploadEvent);
                    WaitForSingleObject(uploadEvent, 5000);
                    CloseHandle(uploadEvent);
                }
            }
        }

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
            mipLevels = static_cast<UINT>(floor(log2(static_cast<double>(std::max(width, height))))) + 1;
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


//-----------------------------------------
// GetDevice / GetDeviceContext / GetSwapChain
// Return raw COM pointers castable by callers
//-----------------------------------------
void* DX12Renderer::GetDevice()
{
    return m_d3d12Device.Get();
}

void* DX12Renderer::GetDeviceContext()
{
    return m_commandList.Get();
}

void* DX12Renderer::GetSwapChain()
{
    return m_swapChain.Get();
}

//-----------------------------------------
// WaitToFinishThenPauseThread
// Drain GPU work, wait for the render loop to idle, then pause the thread.
//-----------------------------------------
void DX12Renderer::WaitToFinishThenPauseThread()
{
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: WaitToFinishThenPauseThread() - beginning safe thread pause");
#endif

    ThreadLockHelper exclusiveLock(threadManager, "exclusive_directx_access", 10000);
    if (!exclusiveLock.IsLocked()) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WaitToFinishThenPauseThread() - failed to acquire exclusive lock");
#endif
        return;
    }

    // Wait for the render loop to finish the current frame
    int waitAttempts = 0;
    while (threadManager.threadVars.bIsRendering.load() && waitAttempts < 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ++waitAttempts;
    }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    if (waitAttempts >= 500)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12Renderer: WaitToFinishThenPauseThread() - timeout waiting for render, forcing pause");
#endif

    // Flush GPU queue to ensure all commands are processed
    try {
        WaitForGPUToFinish();
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12Renderer: WaitToFinishThenPauseThread() - GPU wait exception: %hs", e.what());
#else
        (void)e;
#endif
    }

    // Pause the renderer thread
    ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
    if (status == ThreadStatus::Running) {
        threadManager.PauseThread(THREAD_RENDERER);

        int pauseAttempts = 0;
        while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running && pauseAttempts < 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++pauseAttempts;
        }
    }

    // Final drain — ensure bIsRendering is clear
    waitAttempts = 0;
    while (threadManager.threadVars.bIsRendering.load() && waitAttempts < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ++waitAttempts;
    }

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12Renderer: WaitToFinishThenPauseThread() - completed");
#endif
}

//-----------------------------------------
// DrawMyTextStyled
// Renders text with bold / italic / underline / strikethrough / centered support.
//-----------------------------------------
void DX12Renderer::DrawMyTextStyled(const std::wstring& text, const Vector2& position,
    const MyColor& color, const TextRenderStyle& style)
{
    if (!IsDX11CompatibilityAvailable() || !m_d2dContext || !m_dwriteFactory) return;
    if (text.empty() || style.fontSize <= 0.0f) return;

    const wchar_t* fontName = style.fontName.empty() ? L"Arial" : style.fontName.c_str();
    DWRITE_FONT_WEIGHT weight = style.bold   ? DWRITE_FONT_WEIGHT_BOLD   : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE  fstyle = style.italic ? DWRITE_FONT_STYLE_ITALIC  : DWRITE_FONT_STYLE_NORMAL;

    IDWriteTextFormat* fmt = GetOrCreateTextFormat(fontName, style.fontSize, weight, fstyle);
    if (!fmt) return;

    UINT32 textLen = static_cast<UINT32>(text.size());

    float layoutWidth = 2000.0f;
    float drawX = position.x;
    if (style.centered) {
        float rtWidth = (m_renderTargetWidth > 0) ? static_cast<float>(m_renderTargetWidth) : 1920.0f;
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layoutWidth = rtWidth;
        drawX = 0.0f;
    }

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(text.c_str(), textLen, fmt, layoutWidth, 500.0f, &layout);
    if (FAILED(hr) || !layout) return;

    DWRITE_TEXT_RANGE all{ 0, textLen };
    if (style.underline)      layout->SetUnderline(TRUE, all);
    if (style.strikethrough)  layout->SetStrikethrough(TRUE, all);

    float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    if (!SetGeneralBrushColor(r, g, b, a)) return;

    m_d2dContext->DrawTextLayout(D2D1::Point2F(drawX, position.y), layout.Get(), m_generalBrush.Get());
}

#endif // defined(__USE_DIRECTX_12__)
