#define NOMINMAX

#include "Includes.h"

// DirectX 11 Required Headers & Linking
#include "Renderer.h"

// Perform Renderer to USE Test.
// This is done to ensure we only include required code.
// Meaning, if we are NOT using this Renderer, forget it
// and DO NOT include its code.
#if defined(__USE_DIRECTX_11__)
#include "DX11Renderer.h"
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

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// Static member initialization
std::mutex DX11Renderer::s_renderMutex;

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
DX11Renderer::DX11Renderer() : m_featureLevel(D3D_FEATURE_LEVEL_11_0)
{ 
    // IMPORTANT: Set the RendererType to DirectX 11 SO that the Engine knows which renderer to use and refer too.
    sName = threadManager.getThreadName(THREAD_RENDERER);
    RenderType = RendererType::RT_DirectX11;
	SecureZeroMemory(&m_d2dTextures, sizeof(m_d2dTextures));
	SecureZeroMemory(&m_d3dTextures, sizeof(m_d3dTextures));
    SecureZeroMemory(&My2DBlitQueue, sizeof(My2DBlitQueue));
    SecureZeroMemory(&screenModes, sizeof(screenModes));
}

DX11Renderer::~DX11Renderer() 
{ 
    if (bIsDestroyed.load()) return;

    Cleanup(); 
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Renderer Cleaned up and Destroyed!");
    bIsDestroyed.store(true);
}

//-----------------------------------------
// Core Rendering Interface
//-----------------------------------------
void DX11Renderer::Initialize(HWND hwnd, HINSTANCE hInstance) {
	// Set the Renderer Name
    RendererName(RENDERER_NAME);
    
    iOrigWidth = winMetrics.clientWidth;
    iOrigHeight = winMetrics.clientHeight;

    // Initilize Direct2D & Direct3D 11 Device and Swap Chain
    CreateDeviceAndSwapChain(hwnd);
    CreateDirect2DResources();
    CreateRenderTargetViews();
    CreateDepthStencilBuffer();
    SetupViewport();
    SetupPipelineStates();
    LoadShaders();

    // Camera Constant Buffer creation
    D3D11_BUFFER_DESC camBufferDesc = {};
    camBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    camBufferDesc.ByteWidth = sizeof(ConstantBuffer);
    camBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    camBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    camBufferDesc.MiscFlags = 0;
    camBufferDesc.StructureByteStride = 0;

    HRESULT hrCam = m_d3dDevice->CreateBuffer(&camBufferDesc, nullptr, m_cameraConstantBuffer.GetAddressOf());
    if (FAILED(hrCam))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to create Camera Constant Buffer. HRESULT = 0x%08X", hrCam);
        return;
    }

    // Initialize our Camera to default values
    if (!threadManager.threadVars.bIsResizing.load())
    {
        myCamera.SetupDefaultCamera(iOrigWidth, iOrigHeight);
    }

    // Create Global Light Buffer
    D3D11_BUFFER_DESC lightCBDesc = {};
    lightCBDesc.ByteWidth = sizeof(GlobalLightBuffer);                                              // structure defined in ConstantBuffer.h
    lightCBDesc.Usage = D3D11_USAGE_DYNAMIC;
    lightCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lightCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_d3dDevice->CreateBuffer(&lightCBDesc, nullptr, &m_globalLightBuffer);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX11Renderer: Failed to create global light buffer.");
        return;
    }

#if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
    // Create Debug Constant Buffer
    D3D11_BUFFER_DESC debugCBDesc = {};
    debugCBDesc.ByteWidth = sizeof(DebugBuffer);
    debugCBDesc.Usage = D3D11_USAGE_DYNAMIC;
    debugCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    debugCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_d3dDevice->CreateBuffer(&debugCBDesc, nullptr, &m_debugBuffer);
    if (FAILED(hr)) 
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DX11Renderer: Failed to create Debug Constant Buffer.");
        return;
    }
#endif

    sysUtils.DisableMouseCursor();
	
    bIsInitialized.store(true);
    if (threadManager.threadVars.bIsResizing.load())
    {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Rendering Engine Initialised and Activated.");
    }
    else
    {
        // We are resizing the window, so restart the loading sequence.
        threadManager.ResumeThread(THREAD_LOADER);
    }

    threadManager.threadVars.bIsResizing.store(false);
}

bool DX11Renderer::StartRendererThreads()
{
    bool result = true;
    try
    {
        // Initialise and Start the Loader Thread
        threadManager.SetThread(THREAD_LOADER, [this]() { LoaderTaskThread(); }, true);
        threadManager.StartThread(THREAD_LOADER);
        // Initialize & start the renderer thread
#ifdef RENDERER_IS_THREAD
        threadManager.SetThread(THREAD_RENDERER, [this]() { RenderFrame(); }, true);
        threadManager.StartThread(THREAD_RENDERER);
#endif
    }
    catch (const std::exception& e)
    {
        result = false;
    }

    return result;
}

ComPtr<ID3D11DeviceContext> DX11Renderer::GetImmediateContext() const {
    return m_d3dContext;
}

void DX11Renderer::Clear2DBlitQueue()
{
    for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
    {
        SecureZeroMemory(&My2DBlitQueue[iX], sizeof(GFXObjQueue));
    }
}

bool DX11Renderer::Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl, BlitObj2DType objType, BlitObj2DDetails objDetails, CanBlitType BlitType)
{
    //////////////////////////////////////////////////
    // Check if the object is already in the queue
    //////////////////////////////////////////////////
    for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
    {
        // We need to find single objects only and check if already in the queue.
        switch (BlitType)
        {
            // Is the Queue Object of the following:-
        case CanBlitType::CAN_BLIT_SINGLE:
            if (My2DBlitQueue[iX].bInUse)
            {
                if (My2DBlitQueue[iX].BlitObjDetails.iBlitID == iIndex)
                {
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
            return TRUE;
        }
    }

    // No empty slots found / LOGIC ERROR!
    return FALSE;
}

void DX11Renderer::Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight)
{
    // Check if the object is valid
    if (int(iIndex) < 0 || int(iIndex) > MAX_TEXTURE_BUFFERS)
    {
        // Handle the error case where the index is out of bounds
        ThrowError("Out of Range Error: Invalid index in Blit2DObject");
        exit(EXIT_FAILURE);
    }

    if ((m_d2dTextures[int(iIndex)]) && (m_d2dRenderTarget))
    {
        // Get the size of the bitmap
        D2D1_SIZE_F bitmapSize = m_d2dTextures[int(iIndex)]->GetSize();

        // Define the destination rectangle where the bitmap will be drawn
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iX),
            static_cast<float>(iY),
            static_cast<float>(iX) + iOrigWidth,
            static_cast<float>(iY) + iOrigHeight
        );

        // Define the source rectangle (entire bitmap)
        D2D1_RECT_F srcRect = D2D1::RectF(
            0.0f,
            0.0f,
            bitmapSize.width,
            bitmapSize.height
        );

        // Draw the bitmap to the render target
        m_d2dRenderTarget->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),                                                       // The bitmap to draw
            destRect,                                                                               // Destination rectangle
            1.0f,                                                                                   // Opacity
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                                                  // Interpolation mode
            srcRect                                                                                 // Source rectangle
        );
    }
    else
    {
        // Handle the error case where the texture or render target is not valid
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Invalid texture or render target in Blit2DObject()");
    }
}

void DX11Renderer::Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY)
{
    // Check if the object is valid
	if (int(iIndex) < 0 || int(iIndex) > MAX_TEXTURE_BUFFERS)
	{
		// Handle the error case where the index is out of bounds
		ThrowError("Out of Range Error: Invalid index in Blit2DObject");
		exit (EXIT_FAILURE);
	}

    if ((m_d2dTextures[int(iIndex)]) && (m_d2dRenderTarget))
    {
        // Get the size of the bitmap
        D2D1_SIZE_F bitmapSize = m_d2dTextures[int(iIndex)]->GetSize();

        // Define the destination rectangle where the bitmap will be drawn
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iX),
            static_cast<float>(iY),
            static_cast<float>(iX) + bitmapSize.width,
            static_cast<float>(iY) + bitmapSize.height
        );

        // Define the source rectangle (entire bitmap)
        D2D1_RECT_F srcRect = D2D1::RectF(
            0.0f,
            0.0f,
            bitmapSize.width,
            bitmapSize.height
        );

        // Draw the bitmap to the render target
        m_d2dRenderTarget->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),                                                       // The bitmap to draw
            destRect,                                                                               // Destination rectangle
            1.0f,                                                                                   // Opacity
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,                                                  // Interpolation mode
            srcRect                                                                                 // Source rectangle
        );
    }
    else
    {
        // Handle the error case where the texture or render target is not valid
		debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Invalid texture or render target in Blit2DObject()");
    }
}

//-------------------------------------------------------------------------------------------------
// Blit2DColoredPixel - Renders a single 1x1 pixel at (x, y) using the specified RGBA color.
// Utilizes Direct2D render target for immediate 2D pixel output.
//-------------------------------------------------------------------------------------------------
void DX11Renderer::Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color) 
{
    if (!m_d2dRenderTarget || threadManager.threadVars.bIsResizing.load()) return;

    // Create or reuse a blend state for alpha blending
    static ComPtr<ID2D1SolidColorBrush> pBrush;
    if (!pBrush) 
    {
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(color.x, color.y, color.z, color.w), &pBrush);
    }
    else 
    {
        pBrush->SetColor(D2D1::ColorF(color.x, color.y, color.z, color.w));
    }

    D2D1_RECT_F pixelRect = D2D1::RectF((FLOAT)x, (FLOAT)y, (FLOAT)x + pixelSize, (FLOAT)y + pixelSize);
    m_d2dRenderTarget->FillRectangle(&pixelRect, pBrush.Get());
}

void DX11Renderer::Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY)
{
    // Check if the object is valid
    if (int(iIndex) < 0 || int(iIndex) > MAX_TEXTURE_BUFFERS)
    {
        // Handle the error case where the index is out of bounds
        ThrowError("Out of Range Error: Invalid index in Blit2DObject");
        exit(EXIT_FAILURE);
    }

    if ((m_d2dTextures[int(iIndex)]) && (m_d2dRenderTarget))
    {
        // Get the size of the bitmap
        D2D1_SIZE_F bitmapSize = m_d2dTextures[int(iIndex)]->GetSize();

        // Define the destination rectangle where the bitmap will be drawn
        D2D1_RECT_F destRect = D2D1::RectF(
            static_cast<float>(iBlitX),
            static_cast<float>(iBlitY),
            static_cast<float>(iBlitX + iTileSizeX),
            static_cast<float>(iBlitY + iTileSizeY)
        );

        // Define the source rectangle (entire bitmap)
        float fXOffset = float(iXOffset);
        float fYOffset = float(iYOffset);
        D2D1_RECT_F srcRect = D2D1::RectF(
            fXOffset,
            fYOffset,
            static_cast<float>(fXOffset + iTileSizeX),
            static_cast<float>(fYOffset + iTileSizeY)
        );

        // Draw the bitmap to the render target
        m_d2dRenderTarget->DrawBitmap(
            m_d2dTextures[int(iIndex)].Get(),       // The bitmap to draw
            destRect,                               // Destination rectangle
            1.0f,                                   // Opacity
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,  // Interpolation mode
            srcRect                                 // Source rectangle
        );
    }
    else
    {
        // Handle the error case where the texture or render target is not valid
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Invalid texture or render target in Blit2DObject()");
    }
}

// This call is handy for tiled image scrolling.
void DX11Renderer::Blit2DWrappedObjectAtOffset(
    BlitObj2DIndexType iIndex,
    int iBlitX,
    int iBlitY,
    int iXOffset,
    int iYOffset,
    int iTileSizeX,
    int iTileSizeY)
{
    if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) return;
    if (!m_d2dTextures[int(iIndex)] || !m_d2dRenderTarget) return;

    ID2D1Bitmap* bitmap = m_d2dTextures[int(iIndex)].Get();
    D2D1_SIZE_F bmpSize = bitmap->GetSize();
    int bmpW = static_cast<int>(bmpSize.width);
    int bmpH = static_cast<int>(bmpSize.height);

    if (bmpW <= 0 || bmpH <= 0) return;

    // Normalize offsets to wrap within source image bounds
    iXOffset = ((iXOffset % bmpW) + bmpW) % bmpW;
    iYOffset = ((iYOffset % bmpH) + bmpH) % bmpH;

    // First tile region (from offset to edge)
    int srcW1 = bmpW - iXOffset;
    int srcH1 = bmpH - iYOffset;

    // Corresponding dest size based on full stretch
    float scaleX = static_cast<float>(iTileSizeX) / bmpW;
    float scaleY = static_cast<float>(iTileSizeY) / bmpH;

    int destW1 = static_cast<int>(srcW1 * scaleX);
    int destH1 = static_cast<int>(srcH1 * scaleY);

    // Always render all 4 possible tiles
    // Part 1: Bottom-right (main part)
    D2D1_RECT_F src1 = D2D1::RectF((float)iXOffset, (float)iYOffset, (float)bmpW, (float)bmpH);
    D2D1_RECT_F dest1 = D2D1::RectF((float)iBlitX, (float)iBlitY, (float)(iBlitX + destW1), (float)(iBlitY + destH1));
    m_d2dRenderTarget->DrawBitmap(bitmap, dest1, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src1);

    // Part 2: Bottom-left (wrap X)
    if (destW1 < iTileSizeX)
    {
        int wrapW = iTileSizeX - destW1;
        D2D1_RECT_F src2 = D2D1::RectF(0, (float)iYOffset, (float)(bmpW - srcW1), (float)bmpH);
        D2D1_RECT_F dest2 = D2D1::RectF((float)(iBlitX + destW1), (float)iBlitY, (float)(iBlitX + iTileSizeX), (float)(iBlitY + destH1));
        m_d2dRenderTarget->DrawBitmap(bitmap, dest2, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src2);
    }

    // Part 3: Top-right (wrap Y)
    if (destH1 < iTileSizeY)
    {
        int wrapH = iTileSizeY - destH1;
        D2D1_RECT_F src3 = D2D1::RectF((float)iXOffset, 0, (float)bmpW, (float)(bmpH - srcH1));
        D2D1_RECT_F dest3 = D2D1::RectF((float)iBlitX, (float)(iBlitY + destH1), (float)(iBlitX + destW1), (float)(iBlitY + iTileSizeY));
        m_d2dRenderTarget->DrawBitmap(bitmap, dest3, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src3);
    }

    // Part 4: Top-left corner (wrap X and Y)
    if (destW1 < iTileSizeX && destH1 < iTileSizeY)
    {
        D2D1_RECT_F src4 = D2D1::RectF(0, 0, (float)(bmpW - srcW1), (float)(bmpH - srcH1));
        D2D1_RECT_F dest4 = D2D1::RectF((float)(iBlitX + destW1), (float)(iBlitY + destH1), (float)(iBlitX + iTileSizeX), (float)(iBlitY + iTileSizeY));
        m_d2dRenderTarget->DrawBitmap(bitmap, dest4, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src4);
    }
}

void DX11Renderer::Cleanup() {
    if (bHasCleanedUp) { return; }
    // Synchronise Thread Closures
//	ThreadStatus status = threadManager.GetThreadStatus(THREAD_LOADER);
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

    // Now clean up the Thread Manager.
    threadManager.Cleanup();

	// Release our 2D textures
    for (int i = 0; i < MAX_TEXTURE_BUFFERS; i++) { m_d2dTextures[i].Reset(); m_d2dTextures[i] = nullptr; }

	// Release our 3D textures
    for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; i++) { m_d3dTextures[i].Reset(); m_d3dTextures[i] = nullptr; }

    // Destory Base Models
    for (int i = 0; i < MAX_MODELS; ++i)
    {
        models[i].DestroyModel();  // <-- Force destroy global base models BEFORE anything reloads
    }

	// Destory our Scene Models
    scene.CleanUp();

    // Release Direct2D & 3D resources
	m_d2dDevice.Reset();
	m_d2dContext.Reset();
    m_d2dRenderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    m_depthStencilBuffer.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
    m_swapChain.Reset();
    m_wireframeState.Reset();
    m_globalLightBuffer.Reset();
	m_cameraConstantBuffer.Reset();

// Only ever included during development debugging
#if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_RENDER_WIREFRAME_)
    m_wireframeState.Reset();
#endif

#if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
	m_debugBuffer.Reset();
#endif
    
    sysUtils.EnableMouseCursor();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Renderer Successfully Cleaned Up.");
    bHasCleanedUp = true;
}

//-----------------------------------------
// Device Management
//-----------------------------------------
void DX11Renderer::CreateDeviceAndSwapChain(HWND hwnd) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    adapter = SelectBestAdapter();

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selectedFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        adapter.Get(),                // Adapter chosen above
        D3D_DRIVER_TYPE_UNKNOWN,      // Must use UNKNOWN when providing adapter
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &selectedFeatureLevel,
        &m_d3dContext
    );
	if (FAILED(hr)) throw std::runtime_error("CRITICAL: Failed to create D3D11 device");

    // Swap chain setup
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) throw std::runtime_error("CRITICAL: Failed to retrieve DXGI device");

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) throw std::runtime_error("CRITICAL: Failed to get DXGI adapter");

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) throw std::runtime_error("CRITICAL: Failed to get DXGI factory");

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = 0; // Automatic sizing
    swapDesc.Height = 0;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.SampleDesc.Count = config.myConfig.msaaEnabled ? 4 : 1;
    swapDesc.SampleDesc.Quality = config.myConfig.msaaEnabled ? (DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN - 1) : 0;    
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2; // Triple Buffer.
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
//    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        m_d3dDevice.Get(),
        hwnd,
        &swapDesc,
        nullptr,
        nullptr,
        &m_swapChain
    );
    if (FAILED(hr)) throw std::runtime_error("CRITICAL: Failed to create swap chain");
}

void DX11Renderer::CreateDirect2DResources() {
    // D2D factory
    D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &options, reinterpret_cast<void**>(m_d2dFactory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create D2D1Factory1");
        ThrowError("Failed to create D2D1Factory1");
    }

    // DWrite factory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create DWriteFactory");
        ThrowError("Failed to create DWriteFactory");
    }

    // Obtain DXGI device from Direct3D device
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to get IDXGIDevice from Direct3D device");
        ThrowError("Failed to get IDXGIDevice from Direct3D device");
    }

    // Use D2D1CreateDevice for compatibility
    hr = D2D1CreateDevice(dxgiDevice.Get(), nullptr, &m_d2dDevice);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create Direct2D device");
        ThrowError("Failed to create Direct2D device");
    }

    // Create Direct2D device context
    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create Direct2D device context");
        ThrowError("Failed to create Direct2D device context");
    }

    // Obtain DXGI surface from swap chain
    ComPtr<IDXGISurface1> dxgiSurface;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to get DXGI surface from swap chain");
        ThrowError("Failed to get DXGI surface from swap chain");
    }

    // Create Direct2D render target from DXGI surface
    ComPtr<ID2D1RenderTarget> d2dRenderTarget;
    D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, // Default render target type
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),          // Pixel format
        0, // Default DPI for X
        0, // Default DPI for Y
        D2D1_RENDER_TARGET_USAGE_NONE, // No special usage
        D2D1_FEATURE_LEVEL_DEFAULT // Default feature level
    );

    // Create the Direct2D render target from the DXGI surface
    hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(
        dxgiSurface.Get(),                                                              // DXGI surface
        &renderTargetProperties,                                                        // Render target properties
        &d2dRenderTarget                                                                // Output render target
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create Direct2D render target from DXGI surface");
        ThrowError("Failed to create Direct2D render target from DXGI surface");
        return;
    }

    // Store the render target
    m_d2dRenderTarget = d2dRenderTarget;

    // Log success
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Direct2D render target created successfully");
}

//-----------------------------------------
// Rendering Operations
//-----------------------------------------
XMFLOAT4 DX11Renderer::ConvertColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return {
        r / 255.0f,
        g / 255.0f,
        b / 255.0f,
        a / 255.0f
    };
}

float DX11Renderer::GetCharacterWidth(wchar_t character, float FontSize) {
    if (!m_dwriteFactory) {
        ThrowError("DirectWrite factory is not initialized.");
        return 0.0f;
    }

    ComPtr<IDWriteTextFormat> m_txtFormat;
    m_dwriteFactory->CreateTextFormat(FontName, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        FontSize, L"en-us", &m_txtFormat);

    // Create a text layout for the character
    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        &character,                                         // The character to measure
        1,                                                  // Length of the character
        m_txtFormat.Get(),                                  // Current text format
        1000.0f,                                            // Maximum width (arbitrary large value)
        1000.0f,                                            // Maximum height (arbitrary large value)
        &textLayout                                         // Output text layout
    );

    if (FAILED(hr)) {
        ThrowError("Failed to create text layout for character width calculation.");
        return 0.0f;
    }

    // Get the metrics of the text layout
    DWRITE_TEXT_METRICS textMetrics;
    hr = textLayout->GetMetrics(&textMetrics);
    if (FAILED(hr)) {
        ThrowError("Failed to get text metrics for character width calculation.");
        return 0.0f;
    }

    // Return the width of the character
    return textMetrics.width;
}

float DX11Renderer::GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& fontName) {
    // Early exit checks for invalid parameters
    if (!m_dwriteFactory) {
        ThrowError("DirectWrite factory is not initialized.");
        return 0.0f;
    }

    // Create text format with specified font name
    ComPtr<IDWriteTextFormat> m_txtFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        fontName.c_str(),                                   // Use specified font name instead of default
        nullptr,                                            // No font collection (use system fonts)
        DWRITE_FONT_WEIGHT_NORMAL,                          // Normal font weight
        DWRITE_FONT_STYLE_NORMAL,                           // Normal font style
        DWRITE_FONT_STRETCH_NORMAL,                         // Normal font stretch
        FontSize,                                           // Font size parameter
        L"en-us",                                           // Locale identifier
        &m_txtFormat                                        // Output text format
    );

    // Check if text format creation was successful
    if (FAILED(hr)) {
        ThrowError("Failed to create text format for character width calculation with custom font.");
        return 0.0f;
    }

    // Create a text layout for the character
    ComPtr<IDWriteTextLayout> textLayout;
    hr = m_dwriteFactory->CreateTextLayout(
        &character,                                         // The character to measure
        1,                                                  // Length of the character
        m_txtFormat.Get(),                                  // Current text format with custom font
        1000.0f,                                            // Maximum width (arbitrary large value)
        1000.0f,                                            // Maximum height (arbitrary large value)
        &textLayout                                         // Output text layout
    );

    // Check if text layout creation was successful
    if (FAILED(hr)) {
        ThrowError("Failed to create text layout for character width calculation with custom font.");
        return 0.0f;
    }

    // Get the metrics of the text layout
    DWRITE_TEXT_METRICS textMetrics;
    hr = textLayout->GetMetrics(&textMetrics);
    if (FAILED(hr)) {
        ThrowError("Failed to get text metrics for character width calculation with custom font.");
        return 0.0f;
    }

    // Return the width of the character using the specified font
    return textMetrics.width;
}

// This function calculates the X position to center text within a container
float DX11Renderer::CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth)
{
    // Check if DirectWrite factory is initialized
    if (!m_dwriteFactory) {
        // Log error and return 0 if factory is not available
        ThrowError("DirectWrite factory is not initialized.");
        return 0.0f;
    }

    // Create a text format with the specified font size
    ComPtr<IDWriteTextFormat> m_txtFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        FontName,                                               // Use the font name from class member
        nullptr,                                                // No font collection (use system fonts)
        DWRITE_FONT_WEIGHT_NORMAL,                              // Normal font weight
        DWRITE_FONT_STYLE_NORMAL,                               // Normal font style
        DWRITE_FONT_STRETCH_NORMAL,                             // Normal font stretch
        FontSize,                                               // Font size parameter
        L"en-us",                                               // Locale identifier
        &m_txtFormat                                            // Output text format
    );

    // Check if text format creation was successful
    if (FAILED(hr)) {
        ThrowError("Failed to create text format for center position calculation.");
        return 0.0f;
    }

    // Create a text layout for measuring the text dimensions
    ComPtr<IDWriteTextLayout> textLayout;
    hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(),                                           // The text to measure
        static_cast<UINT32>(text.length()),                     // Length of the text (explicitly cast to UINT32)
        m_txtFormat.Get(),                                      // Text format to use
        containerWidth,                                         // Maximum width (use container width)
        1000.0f,                                                // Maximum height (arbitrary large value)
        &textLayout                                             // Output text layout
    );

    // Check if text layout creation was successful
    if (FAILED(hr)) {
        ThrowError("Failed to create text layout for center position calculation.");
        return 0.0f;
    }

    // Get the metrics of the text layout
    DWRITE_TEXT_METRICS textMetrics;
    hr = textLayout->GetMetrics(&textMetrics);

    // Check if metrics retrieval was successful
    if (FAILED(hr)) {
        ThrowError("Failed to get text metrics for center position calculation.");
        return 0.0f;
    }

    // Calculate the X position that would center the text in the container
    // For perfect centering, we take half of the container width and subtract half of the text width
    float centerX = (containerWidth - textMetrics.width) / 2.0f;

    // Ensure we don't return a negative position (minimum 0)
    return (centerX < 0.0f) ? 0.0f : centerX;
}

float DX11Renderer::CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) {
    if (!m_dwriteFactory) {
        ThrowError("DirectWrite factory or text format is not initialized.");
        return 0.0f;
    }

    ComPtr<IDWriteTextFormat> m_txtFormat;
    m_dwriteFactory->CreateTextFormat(FontName, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        FontSize, L"en-us", &m_txtFormat);

    // Create a text layout for the text string
    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(),                                                       // The text to measure
        text.length(),                                                      // Length of the text
        m_txtFormat.Get(),                                                  // Current text format
        1000.0f,                                                            // Maximum width (arbitrary large value)
        1000.0f,                                                            // Maximum height (arbitrary large value)
        &textLayout                                                         // Output text layout
    );

    if (FAILED(hr)) {
        ThrowError("Failed to create text layout for text height calculation.");
        return 0.0f;
    }

    // Get the metrics of the text layout
    DWRITE_TEXT_METRICS textMetrics;
    hr = textLayout->GetMetrics(&textMetrics);
    if (FAILED(hr)) {
        ThrowError("Failed to get text metrics for text height calculation.");
        return 0.0f;
    }

    // Return the height of the text
    return textMetrics.height;
}

void DX11Renderer::DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) {
#ifdef RENDERER_IS_THREAD
    std::lock_guard<std::mutex> lock(s_renderMutex);
#endif
    if (is2D) {
        // Direct2D implementation
        if (!m_d2dRenderTarget) return;

        ComPtr<ID2D1SolidColorBrush> brush;
        DirectX::XMFLOAT4 convColor = ConvertColor(color.r, color.g, color.b, color.a);
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(convColor.x, convColor.y, convColor.z, convColor.w), &brush);
        m_d2dRenderTarget->FillRectangle(D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y), brush.Get());
    }
    else {
        // Direct3D implementation
        struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT4 color; };
        DirectX::XMFLOAT4 convColor = ConvertColor(color.r, color.g, color.b, color.a);
        Vertex vertices[] = {
            { { position.x, position.y, 0.0f }, { convColor.x, convColor.y, convColor.z, convColor.w } },
            { { position.x + size.x, position.y, 0.0f }, { convColor.x, convColor.y, convColor.z, convColor.w } },
            { { position.x + size.x, position.y + size.y, 0.0f }, { convColor.x, convColor.y, convColor.z, convColor.w } },
            { { position.x, position.y + size.y, 0.0f }, { convColor.x, convColor.y, convColor.z, convColor.w } }
        };

        ComPtr<ID3D11Buffer> vertexBuffer;
        D3D11_BUFFER_DESC desc = { sizeof(vertices), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
        D3D11_SUBRESOURCE_DATA data = { vertices };
        m_d3dDevice->CreateBuffer(&desc, &data, &vertexBuffer);

        UINT stride = sizeof(Vertex), offset = 0;
        m_d3dContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
        m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_d3dContext->Draw(4, 0);
    }
}

void DX11Renderer::DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight) {
    if (!m_d2dRenderTarget || !m_dwriteFactory) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Direct2D render target or DirectWrite factory is not initialized.");
        return;
    }

    // Create text format
    ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        FontName, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        FontSize, L"en-us", &textFormat
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create text format.");
        return;
    }

    // Measure text size
    ComPtr<IDWriteTextLayout> textLayout;
    hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()),
        textFormat.Get(),  // Use current text format
        1000.0f, 1000.0f,  // Arbitrary large width & height for measuring
        &textLayout
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create text layout.");
        return;
    }

    // Calculate centered position
    // Set text alignment to centered
    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Set paragraph alignment to centered vertically
    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Get text size
    DWRITE_TEXT_METRICS textMetrics;
    textLayout->GetMetrics(&textMetrics);

    float centeredX = position.x + (controlWidth / 2.0f) - (textMetrics.width / 2.0f);
    float centeredY = position.y + (controlHeight / 2.0f) - (textMetrics.height / 2.0f);

    // Create solid color brush
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);

    // Draw text centered within the button's bounds
    m_d2dRenderTarget->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        textFormat.Get(),
        D2D1::RectF(centeredX, centeredY, centeredX + textMetrics.width, centeredY + textMetrics.height),
        brush.Get()
    );
}

void DX11Renderer::DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) {
    // Early exit checks
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    // Create text format - keep it simple
    ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        FontName, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        FontSize, L"en-us", &textFormat
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DrawMyText: Failed to create text format");
        return;
    }

    // CORRECTED: Convert MyColor uint8_t (0-255) to float (0.0-1.0) for Direct2D
    float r = static_cast<float>(color.r) / 255.0f;                            // Convert red from 0-255 to 0.0-1.0
    float g = static_cast<float>(color.g) / 255.0f;                            // Convert green from 0-255 to 0.0-1.0
    float b = static_cast<float>(color.b) / 255.0f;                            // Convert blue from 0-255 to 0.0-1.0
    float a = static_cast<float>(color.a) / 255.0f;                            // Convert alpha from 0-255 to 0.0-1.0

    // Create brush with the correct normalized values
    ComPtr<ID2D1SolidColorBrush> brush;
    hr = m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &brush);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DrawMyText: Failed to create brush");
        return;
    }

    // Simple destination rectangle
    D2D1_RECT_F destRect = D2D1::RectF(
        position.x,
        position.y,
        position.x + 1000.0f,
        position.y + 200.0f
    );

    // Render the text with transparency support
    m_d2dRenderTarget->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        textFormat.Get(),
        destRect,
        brush.Get()
    );

/*
#if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
    // Debug transparency values
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"DrawMyText: MyColor(%d,%d,%d,%d) -> Float(%.3f,%.3f,%.3f,%.3f) Text='%s'",
        color.r, color.g, color.b, color.a, r, g, b, a, text.substr(0, 20).c_str());
#endif
*/
}

void DX11Renderer::DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) {
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;

    // Create text format if missing
    ComPtr<IDWriteTextFormat> m_txtFormat;
    m_dwriteFactory->CreateTextFormat(FontName, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                      FontSize, L"en-us", &m_txtFormat);

    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);

    std::wstring wtext(text.begin(), text.end());
    m_d2dRenderTarget->DrawText(
        wtext.c_str(),
        static_cast<UINT32>(wtext.size()),
        m_txtFormat.Get(),
        D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y), brush.Get()
    );
}

void DX11Renderer::DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color,
    const float FontSize, const std::wstring& fontName) {
    // Early exit checks
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    // Create text format with specified font name - keep it simple
    ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        fontName.c_str(),                                                       // Use specified font name
        nullptr,                                                                // No font collection
        DWRITE_FONT_WEIGHT_NORMAL,                                              // Normal font weight
        DWRITE_FONT_STYLE_NORMAL,                                               // Normal font style
        DWRITE_FONT_STRETCH_NORMAL,                                             // Normal font stretch
        FontSize,                                                               // Font size parameter
        L"en-us",                                                               // Locale identifier
        &textFormat                                                             // Output text format
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DrawMyTextWithFont: Failed to create text format with font: " + fontName);
        return;
    }

    // CORRECTED: Convert MyColor uint8_t (0-255) to float (0.0-1.0) for Direct2D
    float r = static_cast<float>(color.r) / 255.0f;                            // Convert red from 0-255 to 0.0-1.0
    float g = static_cast<float>(color.g) / 255.0f;                            // Convert green from 0-255 to 0.0-1.0
    float b = static_cast<float>(color.b) / 255.0f;                            // Convert blue from 0-255 to 0.0-1.0
    float a = static_cast<float>(color.a) / 255.0f;                            // Convert alpha from 0-255 to 0.0-1.0

    // Create brush with the correct normalized values
    ComPtr<ID2D1SolidColorBrush> brush;
    hr = m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &brush);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DrawMyTextWithFont: Failed to create brush");
        return;
    }

    // Simple destination rectangle
    D2D1_RECT_F destRect = D2D1::RectF(
        position.x,
        position.y,
        position.x + 1000.0f,
        position.y + 200.0f
    );

    // Render the text with transparency support using specified font
    m_d2dRenderTarget->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        textFormat.Get(),                                                       // Use custom text format with specified font
        destRect,
        brush.Get()
    );

#if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
    // Debug font and transparency values
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"DrawMyTextWithFont: Font='%s' MyColor(%d,%d,%d,%d) -> Float(%.3f,%.3f,%.3f,%.3f) Text='%s'",
        fontName.c_str(), color.r, color.g, color.b, color.a, r, g, b, a, text.substr(0, 20).c_str());
#endif
}

void DX11Renderer::DrawTexture(int textureIndex, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) {
#ifdef RENDERER_IS_THREAD
    std::lock_guard<std::mutex> lock(s_renderMutex);
#endif

    if (is2D) {
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS || !m_d2dTextures[textureIndex]) return;

        // Convert tintColor to D2D1_COLOR_F
        D2D1_COLOR_F d2dTintColor = D2D1::ColorF(
            tintColor.r,
            tintColor.g,
            tintColor.b,
            tintColor.a
        );

        // Create a color brush for tinting if needed
        ComPtr<ID2D1SolidColorBrush> tintBrush;
        m_d2dRenderTarget->CreateSolidColorBrush(d2dTintColor, &tintBrush);

        // The correct Direct2D DrawBitmap signature for DX11:
        m_d2dRenderTarget->DrawBitmap(
            m_d2dTextures[textureIndex].Get(),
            D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y),
            tintColor.a,  // opacity
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            nullptr  // source rectangle (nullptr means entire bitmap)
        );
    }
    else {
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D || !m_d3dTextures[textureIndex]) return;

        struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; };
        Vertex vertices[] = {
            { { position.x, position.y, 0.0f }, { 0.0f, 0.0f } },
            { { position.x + size.x, position.y, 0.0f }, { 1.0f, 0.0f } },
            { { position.x + size.x, position.y + size.y, 0.0f }, { 1.0f, 1.0f } },
            { { position.x, position.y + size.y, 0.0f }, { 0.0f, 1.0f } }
        };

        ComPtr<ID3D11Buffer> vertexBuffer;
        D3D11_BUFFER_DESC desc = { sizeof(vertices), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
        D3D11_SUBRESOURCE_DATA data = { vertices };
        m_d3dDevice->CreateBuffer(&desc, &data, &vertexBuffer);

        m_d3dContext->PSSetShaderResources(0, 1, m_d3dTextures[textureIndex].GetAddressOf());
        UINT stride = sizeof(Vertex), offset = 0;
        m_d3dContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
        m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_d3dContext->Draw(4, 0);
    }
}

void DX11Renderer::RendererName(std::string sThisName)
{
    sName = sThisName;
}

//DrawVideoFrame method
void DX11Renderer::DrawVideoFrame(const Vector2& position, const Vector2& size, const MyColor& tintColor, ComPtr<ID3D11Texture2D> videoTexture)
{
    // Early exit if no valid texture
    if (!videoTexture || !m_d2dRenderTarget || !m_d3dContext) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Missing D2D resources in DrawVideoFrame");
        return;
    }

    // We're already in the renderer mutex scope from the caller
    // No need to acquire it again to avoid deadlocks

    try {
        // Get the texture description to determine dimensions
        D3D11_TEXTURE2D_DESC textureDesc = {};
        videoTexture->GetDesc(&textureDesc);

        // Create a staging texture for CPU access if needed
        ComPtr<ID3D11Texture2D> stagingTexture;
        D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
        if (FAILED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create staging texture");
            return;
        }

        // Copy the video texture to the staging texture
        m_d3dContext->CopyResource(stagingTexture.Get(), videoTexture.Get());

        // Map the staging texture to get pixel data
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = m_d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to map staging texture");
            return;
        }

        // Check if Direct2D is currently being used for rendering
        // Try to acquire the D2D draw lock
        ThreadLockHelper d2dLock(threadManager, "d2d_draw_lock", 1000);
        if (!d2dLock.IsLocked()) {
            // If we can't get the lock, unmap and return
            m_d3dContext->Unmap(stagingTexture.Get(), 0);
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D draw lock - skipping video frame");
            return;
        }

        // Create D2D bitmap from the pixel data
        D2D1_SIZE_U bitmapSize = D2D1::SizeU(textureDesc.Width, textureDesc.Height);
        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        ComPtr<ID2D1Bitmap> d2dBitmap;
        hr = m_d2dRenderTarget->CreateBitmap(
            bitmapSize,
            mappedResource.pData,
            mappedResource.RowPitch,
            bitmapProps,
            &d2dBitmap
        );

        // Unmap the staging texture
        m_d3dContext->Unmap(stagingTexture.Get(), 0);

        if (FAILED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create D2D bitmap");
            return;
        }

        // Now draw the bitmap with Direct2D
        D2D1_RECT_F destRect = D2D1::RectF(
            position.x,
            position.y,
            position.x + size.x,
            position.y + size.y
        );

        // Draw the bitmap
        m_d2dRenderTarget->DrawBitmap(
            d2dBitmap.Get(),
            destRect,
            tintColor.a,  // opacity
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Exception in DrawVideoFrame: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

//-----------------------------------------
// Utility Functions
//-----------------------------------------
void DX11Renderer::Resize(uint32_t width, uint32_t height)
{
    std::string lockName = "renderer_resize_lock";
    // Try to acquire the render mutex with a 1000ms timeout
    if (!threadManager.TryLock(lockName, 1000)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire render mutex for resize operation - timeout reached");
        return;
    }

    #if defined(_DEBUG_RENDERER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER]: Resize requested: " + std::to_wstring(width) + L"x" + std::to_wstring(height));
    #endif

    if (!m_swapChain || !m_d3dDevice || !m_d3dContext)
    {
        #if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[RENDERER]: Resize - Missing critical DX interfaces.");
        #endif
        // Release the lock when done
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

        #if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERER]: Saved previous windowed size: " +
            std::to_wstring(prevWindowedWidth) + L"x" + std::to_wstring(prevWindowedHeight));
        #endif
    }

    #if defined(RENDERER_IS_THREAD)
        // Ensure the renderer finishes first!
        while (threadManager.threadVars.bIsRendering.load())
        {
            // Small delay to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Now terminate the Renderer Thread.
        threadManager.PauseThread(THREAD_RENDERER);
    #endif

    // --- Release all references before ResizeBuffers() ---
    if (m_d2dContext)
    {
        m_d2dContext->SetTarget(nullptr);
        m_d2dContext->Flush();
        D2DBusy.store(false);
    }

    // --- Clean old 2D textures using invalidated render targets ---
    m_d2dRenderTarget.Reset();
    m_d2dContext.Reset();
    dxgiSurface.Reset();
    Clean2DTextures();

    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    m_depthStencilBuffer.Reset();

    // Resize swap chain
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        #if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER]: ResizeBuffers failed.");
        #endif
        threadManager.RemoveLock(lockName);
        return;
    }

    // Recreate render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        #if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER]: Failed to retrieve back buffer.");
        #endif
        threadManager.RemoveLock(lockName);
        return;
    }

    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    if (FAILED(hr))
    {
        #if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER]: Failed to create render target view.");
        #endif
        threadManager.RemoveLock(lockName);
        return;
    }

    // Recreate depth stencil buffer
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_d3dDevice->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    hr = m_d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());

    // Bind new targets
    m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    CreateDirect2DResources();

    // Recreate viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports(1, &vp);

    iOrigWidth = width;
    iOrigHeight = height;

    // Recreate D2D target
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
    if (SUCCEEDED(hr) && m_d2dDevice)
    {
        hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
        if (SUCCEEDED(hr))
        {
            D2D1_BITMAP_PROPERTIES1 props = {};
            props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            props.dpiX = 96.0f;
            props.dpiY = 96.0f;
            props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

            ComPtr<ID2D1Bitmap1> targetBitmap;
            hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &props, targetBitmap.GetAddressOf());
            if (SUCCEEDED(hr))
                m_d2dContext->SetTarget(targetBitmap.Get());
        }
    }

    threadManager.threadVars.bIsResizing.store(false);

    // Exit THE render mutex scope
    threadManager.RemoveLock(lockName);
}

void DX11Renderer::ResumeLoader(bool isResizing)
{
    // --- Important to set this flag and have it reset on Load Completion (ENFORCEMENT) ---
    if (isResizing)
       wasResizing.store(true);

    D2DBusy.store(false);
	threadManager.threadVars.bLoaderTaskFinished.store(false);

    ThreadStatus tstat = threadManager.GetThreadStatus(THREAD_LOADER);

    // -------------------------------
    // Now resume THREAD_LOADER safely
    // -------------------------------
    std::thread resumeLoaderThread([this, tstat]() {
        try
        {
            if (tstat == ThreadStatus::Running || tstat == ThreadStatus::Paused)
            {
                threadManager.ResumeThread(THREAD_LOADER);
                #if defined(_DEBUG_RENDERER_)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: THREAD LOADING System Resumed.");
                #endif
            }
            else if (tstat == ThreadStatus::Stopped || tstat == ThreadStatus::Terminated)
            {
                // Set the thread with the correct handler lambda
                threadManager.SetThread(THREAD_LOADER, [this]() {
                    this->LoaderTaskThread(); // safely bound to DX11Renderer instance
                    }, true);

                threadManager.StartThread(THREAD_LOADER);
                #if defined(_DEBUG_RENDERER_)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER]: THREAD LOADING System Restarted.");
                #endif
            }
        }
        catch (const std::exception& e)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER]: Exception during thread resume: " +
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        });

    resumeLoaderThread.detach(); // <== ✅ Properly detach the thread so no crash occurs
}

bool DX11Renderer::LoadAllKnownTextures()
{
    // Load in our required 2D textures
    bool result = true;
    for (int i = 0; i < MAX_TEXTURE_BUFFERS; i++)
    {
        auto fileName = AssetsDir / texFilename[i];
        // Load the texture
        if (!LoadTexture(i, fileName, true))
        {
            std::wstring msg = L"[LOADER]: Failed to load 2D Texture: " / AssetsDir / texFilename[i];
            debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
            return false;
        }
    }

    return result;
}

bool DX11Renderer::LoadTexture(int textureIndex, const std::wstring& filename, bool is2D) {
#ifdef RENDERER_IS_THREAD
    std::lock_guard<std::mutex> lock(s_renderMutex);
#endif

    // Validate texture index
    if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS) {
        ThrowError("Invalid texture index in LoadTexture");
        return false;
    }

    if (is2D) {
        // Ensure Direct2D render target is initialized
        if (!m_d2dRenderTarget) {
            ThrowError("Direct2D render target is not initialized");
            return false;
        }

        // Initialize WIC factory
        ComPtr<IWICImagingFactory> wicFactory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory)
        );

        if (FAILED(hr)) {
            ThrowError("Failed to create WIC factory");
            return false;
        }

        // Create decoder
        ComPtr<IWICBitmapDecoder> decoder;
        hr = wicFactory->CreateDecoderFromFilename(
            filename.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder
        );

        if (FAILED(hr)) {
            ThrowError("Failed to create WIC decoder");
            return false;
        }

        // Get the first frame
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            ThrowError("Failed to get WIC frame");
            return false;
        }

        // Create format converter
        ComPtr<IWICFormatConverter> converter;
        hr = wicFactory->CreateFormatConverter(&converter);
        if (FAILED(hr)) {
            ThrowError("Failed to create WIC format converter");
            return false;
        }

        // Initialize the converter
        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom
        );
        if (FAILED(hr)) {
            ThrowError("Failed to initialize WIC format converter");
            return false;
        }

        // Create Direct2D bitmap
        ComPtr<ID2D1Bitmap> bitmap;
        hr = m_d2dRenderTarget->CreateBitmapFromWicBitmap(converter.Get(), &bitmap);
        if (FAILED(hr)) {
            ThrowError("Failed to create Direct2D bitmap");
            return false;
        }

        // Save our Bitmap Texture.
        m_d2dTextures[textureIndex] = bitmap;
    }
    else 
    {
        // Ensure Direct3D device is initialized
        if (!m_d3dDevice) 
        {
            ThrowError("Direct3D device is not initialized");
            return false;
        }

        // Open the DDS file
        HANDLE file = CreateFileW(
            filename.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (file == INVALID_HANDLE_VALUE) {
            ThrowError("Failed to open DDS file");
            return false;
        }

        // Get file size
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(file, &fileSize)) {
            CloseHandle(file);
            ThrowError("Failed to get DDS file size");
            return false;
        }

        // Read file data
        std::vector<BYTE> fileData(fileSize.LowPart);
        DWORD bytesRead;
        if (!ReadFile(file, fileData.data(), fileSize.LowPart, &bytesRead, nullptr) || bytesRead != fileSize.LowPart) {
            CloseHandle(file);
            ThrowError("Failed to read DDS file");
            return false;
        }
        CloseHandle(file);

        // Validate DDS magic number
        DWORD magicNumber = *reinterpret_cast<DWORD*>(fileData.data());
        if (magicNumber != MAKEFOURCC('D', 'D', 'S', ' ')) {
            ThrowError("Invalid DDS file format");
            return false;
        }

        // Validate file size
        if (fileData.size() < (sizeof(DDS_HEADER) + sizeof(DWORD))) {
            ThrowError("DDS file is too small");
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
            ThrowError("Unsupported DDS format");
            return false;
        }

        // Create texture description
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = header->dwWidth;
        desc.Height = header->dwHeight;
        desc.MipLevels = header->dwMipMapCount ? header->dwMipMapCount : 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        // Create texture
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = fileData.data() + sizeof(DWORD) + sizeof(DDS_HEADER);
        initData.SysMemPitch = static_cast<UINT>(((desc.Width + 3) >> 2) << 3);

        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, &initData, &texture);
        if (FAILED(hr)) {
            ThrowError("Failed to create D3D11 texture");
            return false;
        }

        // Create shader resource view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = m_d3dDevice->CreateShaderResourceView(texture.Get(), &srvDesc, &srv);
        if (FAILED(hr)) {
            ThrowError("Failed to create shader resource view");
            return false;
        }

        // Store the SRV
        m_d3dTextures[textureIndex] = srv;
    }

    return true;
}

void DX11Renderer::UnloadTexture(int textureIndex, bool is2D) {
#ifdef RENDERER_IS_THREAD
    std::lock_guard<std::mutex> lock(s_renderMutex);
#endif
    if (is2D) m_d2dTextures[textureIndex].Reset();
    else m_d3dTextures[textureIndex].Reset();
}

//-----------------------------------------
// Internal Helpers
//-----------------------------------------
void DX11Renderer::CreateRenderTargetViews() {
    // Get the back buffer from the swap chain
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        ThrowError("Failed to get back buffer from swap chain.");
        return;
    }

    // Create the render target view
    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr)) {
        ThrowError("Failed to create render target view.");
        return;
    }

    // Get the back buffer description to ensure consistent dimensions and multisample settings
    D3D11_TEXTURE2D_DESC backBufferDesc;
    backBuffer->GetDesc(&backBufferDesc);

    // Store the dimensions and multisample settings for use in the depth stencil buffer
    m_renderTargetWidth = backBufferDesc.Width;
    m_renderTargetHeight = backBufferDesc.Height;
    m_renderTargetSampleCount = backBufferDesc.SampleDesc.Count;
    m_renderTargetSampleQuality = backBufferDesc.SampleDesc.Quality;
}

void DX11Renderer::CreateDepthStencilBuffer() {
    // Use the same dimensions and multisample settings as the render target view
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_renderTargetWidth;  // Use the same width as the render target
    depthDesc.Height = m_renderTargetHeight; // Use the same height as the render target
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = m_renderTargetSampleCount; // Use the same sample count
    depthDesc.SampleDesc.Quality = m_renderTargetSampleQuality; // Use the same sample quality
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    // Create the depth stencil buffer
    HRESULT hr = m_d3dDevice->CreateTexture2D(&depthDesc, nullptr, &m_depthStencilBuffer);
    if (FAILED(hr)) {
        ThrowError("Failed to create depth stencil buffer.");
        return;
    }

    // Create the depth stencil view
    hr = m_d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, &m_depthStencilView);
    if (FAILED(hr)) {
        ThrowError("Failed to create depth stencil view.");
        return;
    }
}

void DX11Renderer::SetupViewport() {
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(DEFAULT_WINDOW_WIDTH);
    viewport.Height = static_cast<float>(DEFAULT_WINDOW_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports(1, &viewport);
}

void DX11Renderer::SetupPipelineStates() {
    HRESULT hr;

    // --------------------------------
    // Rasterizer State
    // --------------------------------
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;                     // Solid fill mode
    rasterDesc.CullMode = D3D11_CULL_NONE;                      // Perfect for DEBUGGING Models

/*
    if (config.myConfig.BackCulling)
    {
//        rasterDesc.CullMode = D3D11_CULL_NONE;                  // Perfect for OBJ Models (Will look into this at a later date
        rasterDesc.CullMode = D3D11_CULL_BACK;                  // Cull back faces
    }
    else
    {
        rasterDesc.CullMode = D3D11_CULL_FRONT;                 // Cull front faces
    }
*/        
//    rasterDesc.FrontCounterClockwise = false;                   // Standard winding order
    rasterDesc.FrontCounterClockwise = true;                   // Standard winding order
    rasterDesc.DepthBias = 0;
    rasterDesc.DepthBiasClamp = 0.0f;
    rasterDesc.SlopeScaledDepthBias = 0.0f;
    rasterDesc.DepthClipEnable = true;                          // Enable depth clipping
    rasterDesc.ScissorEnable = false;                           // No scissor test
    rasterDesc.MultisampleEnable = config.myConfig.msaaEnabled; // MSAA from config
    rasterDesc.AntialiasedLineEnable = config.myConfig.antiAliasingEnabled;

    // Create rasterizer state
    hr = m_d3dDevice->CreateRasterizerState(&rasterDesc, &m_rasterizerState);
    if (FAILED(hr)) {
        ThrowError("Failed to create rasterizer state!");
        return;
    }

    // Set rasterizer state
    m_d3dContext->RSSetState(m_rasterizerState.Get());

#if defined(_DEBUG_RENDER_WIREFRAME_)
    rasterDesc.FillMode = D3D11_FILL_WIREFRAME;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.MultisampleEnable = false; 
    rasterDesc.AntialiasedLineEnable = config.myConfig.antiAliasingEnabled;
    hr = m_d3dDevice->CreateRasterizerState(&rasterDesc, &m_wireframeState);
    if (FAILED(hr)) {
        ThrowError("Failed to create Wire-Frame Rasterizer State!");
        return;
    }

    // Set rasterizer state
    m_d3dContext->RSSetState(m_wireframeState.Get());
#endif

    // --------------------------------
    // Sampler State
    // --------------------------------
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = config.myConfig.MipMapping ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP; // Wrap texture coordinates
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 16; // Max anisotropy for better texture quality
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER; // No comparison
    sampDesc.BorderColor[0] = 1.0f; // White border color
    sampDesc.BorderColor[1] = 1.0f;
    sampDesc.BorderColor[2] = 1.0f;
    sampDesc.BorderColor[3] = 1.0f;
    sampDesc.MinLOD = 0; // No minimum LOD
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX; // No maximum LOD

    // Create sampler state
    hr = m_d3dDevice->CreateSamplerState(&sampDesc, &m_samplerState);
    if (FAILED(hr)) {
        ThrowError("Failed to create sampler state!");
        return;
    }

    // Set sampler state to pixel shader stage
    m_d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    // --------------------------------
    // Depth-Stencil State (Optional)
    // --------------------------------
    D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthStencilDesc.StencilEnable = false; // No stencil testing

    ComPtr<ID3D11DepthStencilState> depthStencilState;
    hr = m_d3dDevice->CreateDepthStencilState(&depthStencilDesc, &depthStencilState);
    if (FAILED(hr)) {
        ThrowError("Failed to create depth-stencil state!");
        return;
    }

    // Set depth-stencil state
    m_d3dContext->OMSetDepthStencilState(depthStencilState.Get(), 1);

    // --------------------------------
    // Blend State (Optional)
    // --------------------------------
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = false;
    blendDesc.IndependentBlendEnable = false;
    blendDesc.RenderTarget[0].BlendEnable = true;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_d3dDevice->CreateBlendState(&blendDesc, &blendState);
    if (FAILED(hr)) {
        ThrowError("Failed to create blend state!");
        return;
    }

    // Set blend state
    m_d3dContext->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFF);
}

void DX11Renderer::LoadShaders() {
    // Compile shaders to get bytecode blobs
    ComPtr<ID3DBlob> vsBlob = CompileShader(L"ModelVShader.hlsl", "main", "vs_5_0");
    ComPtr<ID3DBlob> psBlob = CompileShader(L"ModelPShader.hlsl", "main", "ps_5_0");

    // Create actual shader objects from the blobs
    m_d3dDevice->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_vertexShader
    );

    m_d3dDevice->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &m_pixelShader
    );

    // Set the shaders to the context
    m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
}

ComPtr<ID3DBlob> DX11Renderer::CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target) {
    ComPtr<ID3DBlob> shaderBlob, errorBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    HRESULT hr = D3DCompileFromFile(
        filename.c_str(),
        nullptr,
        nullptr,
        entryPoint.c_str(),
        target.c_str(),
        flags,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::string errorMsg(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
            ThrowError("Shader compilation failed: " + errorMsg);
        }
        ThrowError("Shader compilation failed with unknown error");
    }
    return shaderBlob;
}

// (Implement per-frame constant buffer updates here)
void DX11Renderer::UpdateConstantBuffers() {
}

void DX11Renderer::ThrowError(const std::string& message) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, std::wstring(message.begin(), message.end()));
    throw std::runtime_error(message);
}

// *-------------------------------------------------------------------------------------*
// Wait for the GPU to finish rendering
// *-------------------------------------------------------------------------------------*
void DX11Renderer::WaitForGPUToFinish()
{
    // Flush the command queue
//	if (m_d3dContext) m_d3dContext->Flush();

    // Ensure all GPU work is completed
    ComPtr<ID3D11Query> query;
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;
    m_d3dDevice->CreateQuery(&queryDesc, &query);
    m_d3dContext->End(query.Get());

    while (m_d3dContext->GetData(query.Get(), NULL, 0, 0) == S_FALSE)
    {
        // Wait for GPU to finish processing
        Sleep(1);
    }
}

void DX11Renderer::Clean2DTextures()
{
    for (int i = 0; i < MAX_TEXTURE_BUFFERS; ++i)
    {
        if (m_d2dTextures[i])
        {
            m_d2dTextures[i].Reset();
#if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERER]: 2D Texture [" + std::to_wstring(i) + L"] released.");
#endif
        }
    }
}

// Set full screen mode.
bool DX11Renderer::SetFullScreen(void)
{
#if defined(_DEBUG_RENDERER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] SetFullScreen() called - beginning fullscreen transition");
#endif

    // Check if already in fullscreen transition to prevent race conditions
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] Fullscreen transition already in progress");
        return false;
    }

    // Set fullscreen transition flag to block other operations
    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    try {
        std::lock_guard<std::mutex> lock(s_renderMutex);                        // Ensure thread safety during fullscreen transition

        // Stop all FX effects before fullscreen transition
        fxManager.StopAllFXForResize();

        // Save current window size before going fullscreen (for potential return to windowed mode)
        RECT rc;
        GetClientRect(hwnd, &rc);
        prevWindowedWidth = rc.right - rc.left;
        prevWindowedHeight = rc.bottom - rc.top;

#if defined(_DEBUG_RENDERER_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERER] Saved windowed size: " +
            std::to_wstring(prevWindowedWidth) + L"x" + std::to_wstring(prevWindowedHeight));
#endif

        // Get the output (monitor) information
        ComPtr<IDXGIOutput> output;
        HRESULT hr = m_swapChain->GetContainingOutput(&output);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get containing output for swap chain");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        DXGI_OUTPUT_DESC outputDesc;
        hr = output->GetDesc(&outputDesc);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get output description");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Calculate new width and height from the monitor
        UINT fullscreenWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        UINT fullscreenHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

#if defined(_DEBUG_RENDERER_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERER] Target fullscreen resolution: " +
            std::to_wstring(fullscreenWidth) + L"x" + std::to_wstring(fullscreenHeight));
#endif

        // Set the fullscreen state first
        hr = m_swapChain->SetFullscreenState(TRUE, nullptr);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to set fullscreen state");
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Set resizing flag to prevent other operations during buffer resize
        threadManager.threadVars.bIsResizing.store(true);

        // Clean up resources before resize
        if (m_d2dContext) {
            m_d2dContext->SetTarget(nullptr);                                   // Clear D2D render target
            m_d2dContext->Flush();                                              // Flush any pending D2D operations
            D2DBusy.store(false);                                               // Mark D2D as not busy
        }

        // Release render target views and other resources
        m_d2dRenderTarget.Reset();                                              // Release D2D render target
        m_d2dContext.Reset();                                                   // Release D2D context
        dxgiSurface.Reset();                                                    // Release DXGI surface
        Clean2DTextures();                                                      // Clean up 2D textures
        m_renderTargetView.Reset();                                             // Release 3D render target
        m_depthStencilView.Reset();                                             // Release depth stencil view
        m_depthStencilBuffer.Reset();                                           // Release depth stencil buffer

        // Resize the buffers to fullscreen resolution
        hr = m_swapChain->ResizeBuffers(
            0,                                                                  // Keep existing buffer count
            fullscreenWidth,                                                    // New width
            fullscreenHeight,                                                   // New height
            DXGI_FORMAT_UNKNOWN,                                                // Keep existing format
            0                                                                   // Default flags
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to resize buffers for fullscreen");
            m_swapChain->SetFullscreenState(FALSE, nullptr);                    // Try to go back to windowed
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Recreate render target view
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get back buffer after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create render target view after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Recreate depth buffer
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = fullscreenWidth;                                      // Set depth buffer width
        depthDesc.Height = fullscreenHeight;                                    // Set depth buffer height
        depthDesc.MipLevels = 1;                                                // Single mip level
        depthDesc.ArraySize = 1;                                                // Single array element
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                      // 24-bit depth, 8-bit stencil
        depthDesc.SampleDesc.Count = 1;                                         // No multisampling
        depthDesc.Usage = D3D11_USAGE_DEFAULT;                                  // Default usage
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;                         // Bind as depth-stencil

        hr = m_d3dDevice->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create depth stencil buffer after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        hr = m_d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create depth stencil view after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Set the viewport for fullscreen
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(fullscreenWidth);                         // Viewport width
        vp.Height = static_cast<float>(fullscreenHeight);                       // Viewport height
        vp.MinDepth = 0.0f;                                                     // Minimum depth
        vp.MaxDepth = 1.0f;                                                     // Maximum depth
        m_d3dContext->RSSetViewports(1, &vp);                                   // Set the viewport

        // Bind the new render target and depth stencil
        m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

        // Update rendering dimensions
        iOrigWidth = fullscreenWidth;                                           // Update width
        iOrigHeight = fullscreenHeight;                                         // Update height

        // Recreate D2D resources
        CreateDirect2DResources();

        // Clear all flags
        threadManager.threadVars.bIsResizing.store(false);
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] Fullscreen mode set successfully");

        return true;
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Exception in SetFullScreen: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));

        // Clear all flags on exception
        threadManager.threadVars.bIsResizing.store(false);
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        return false;
    }
}

bool DX11Renderer::SetFullExclusive(uint32_t width, uint32_t height)
{
#if defined(_DEBUG_RENDERER_)
    // Log the beginning of exclusive fullscreen transition with specified resolution
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[RENDERER] SetFullExclusive(%d, %d) called - beginning exclusive fullscreen transition", width, height);
#endif

    // Check if already in fullscreen transition to prevent race conditions
    if (bFullScreenTransition.load()) {
        // Log warning that transition is already in progress
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] Fullscreen transition already in progress");
        return false;
    }

    // Set fullscreen transition flag to block other operations during transition
    bFullScreenTransition.store(true);
    // Set threading flag to indicate fullscreen mode is being configured
    threadManager.threadVars.bSettingFullScreen.store(true);

    try {
        // Acquire render mutex to ensure thread safety during exclusive fullscreen transition
        std::lock_guard<std::mutex> lock(s_renderMutex);

        // Stop all FX effects before exclusive fullscreen transition to prevent rendering conflicts
        fxManager.StopAllFXForResize();

        // Save current window size before going to exclusive fullscreen (for potential return to windowed mode)
        RECT rc;
        GetClientRect(hwnd, &rc);
        prevWindowedWidth = rc.right - rc.left;
        prevWindowedHeight = rc.bottom - rc.top;

#if defined(_DEBUG_RENDERER_)
        // Log the saved windowed size for debugging purposes
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[RENDERER] Saved windowed size: %dx%d", prevWindowedWidth, prevWindowedHeight);
#endif

        // Get the output (monitor) information to validate the requested resolution
        ComPtr<IDXGIOutput> output;
        HRESULT hr = m_swapChain->GetContainingOutput(&output);
        if (FAILED(hr)) {
            // Log error if we cannot get the containing output for the swap chain
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get containing output for swap chain");
            // Clear transition flags on failure
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Get output description to check available display modes
        DXGI_OUTPUT_DESC outputDesc;
        hr = output->GetDesc(&outputDesc);
        if (FAILED(hr)) {
            // Log error if we cannot get the output description
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get output description");
            // Clear transition flags on failure
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Enumerate available display modes to verify the requested resolution is supported
        UINT numModes = 0;
        DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;

        // Get the number of available display modes for the specified format
        hr = output->GetDisplayModeList(format, 0, &numModes, nullptr);
        if (FAILED(hr) || numModes == 0) {
            // Log error if we cannot enumerate display modes
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to enumerate display modes");
            // Clear transition flags on failure
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Allocate memory for display mode list
        std::vector<DXGI_MODE_DESC> displayModes(numModes);

        // Get the actual display mode list
        hr = output->GetDisplayModeList(format, 0, &numModes, displayModes.data());
        if (FAILED(hr)) {
            // Log error if we cannot get the display mode list
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get display mode list");
            // Clear transition flags on failure
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Find the closest matching display mode for the requested resolution
        DXGI_MODE_DESC targetMode = {};
        targetMode.Width = width;
        targetMode.Height = height;
        targetMode.Format = format;
        targetMode.RefreshRate.Numerator = 60;     // Default to 60Hz refresh rate
        targetMode.RefreshRate.Denominator = 1;    // Standard refresh rate denominator

        DXGI_MODE_DESC closestMode = {};
        hr = output->FindClosestMatchingMode(&targetMode, &closestMode, m_d3dDevice.Get());
        if (FAILED(hr)) {
            // Log error if we cannot find a matching display mode
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to find closest matching display mode");
            // Clear transition flags on failure
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

#if defined(_DEBUG_RENDERER_)
        // Log the closest matching mode found for debugging purposes
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[RENDERER] Closest matching mode: %dx%d @%dHz",
            closestMode.Width, closestMode.Height,
            closestMode.RefreshRate.Numerator / closestMode.RefreshRate.Denominator);
#endif

        // Set resizing flag to prevent other operations during buffer resize
        threadManager.threadVars.bIsResizing.store(true);

        // Clean up Direct2D resources before resize operations
        if (m_d2dContext) {
            // Clear Direct2D render target to release references
            m_d2dContext->SetTarget(nullptr);
            // Flush any pending Direct2D operations
            m_d2dContext->Flush();
            // Mark Direct2D as not busy
            D2DBusy.store(false);
        }

        // Release render target views and other resources before resize
        m_d2dRenderTarget.Reset();      // Release Direct2D render target
        m_d2dContext.Reset();           // Release Direct2D context
        dxgiSurface.Reset();            // Release DXGI surface
        Clean2DTextures();              // Clean up 2D textures
        m_renderTargetView.Reset();     // Release 3D render target
        m_depthStencilView.Reset();     // Release depth stencil view
        m_depthStencilBuffer.Reset();   // Release depth stencil buffer

        // Set to exclusive fullscreen mode with the closest matching mode
        hr = m_swapChain->SetFullscreenState(TRUE, output.Get());
        if (FAILED(hr)) {
            // Log error if setting fullscreen state fails
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to set exclusive fullscreen state");
            // Clear resizing and transition flags on failure
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Resize the swap chain buffers to the closest matching resolution
        hr = m_swapChain->ResizeBuffers(
            0,                          // Keep existing buffer count
            closestMode.Width,          // Use closest matching width
            closestMode.Height,         // Use closest matching height
            closestMode.Format,         // Use closest matching format
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH  // Allow mode switching for exclusive fullscreen
        );

        if (FAILED(hr)) {
            // Log error if resize buffers fails
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to resize buffers for exclusive fullscreen");
            // Try to revert to windowed mode if resize fails
            m_swapChain->SetFullscreenState(FALSE, nullptr);
            // Clear resizing and transition flags on failure
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Recreate render target view after buffer resize
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            // Log error if getting back buffer fails
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get back buffer after resize");
            // Clear resizing and transition flags on failure
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Create new render target view from the back buffer
        hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
        if (FAILED(hr)) {
            // Log error if creating render target view fails
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create render target view after resize");
            // Clear resizing and transition flags on failure
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Recreate depth stencil buffer with the new resolution
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = closestMode.Width;                        // Set depth buffer width to closest mode width
        depthDesc.Height = closestMode.Height;                      // Set depth buffer height to closest mode height
        depthDesc.MipLevels = 1;                                    // Single mip level for depth buffer
        depthDesc.ArraySize = 1;                                    // Single array element
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // 24-bit depth, 8-bit stencil format
        depthDesc.SampleDesc.Count = 1;                             // No multisampling for compatibility
        depthDesc.Usage = D3D11_USAGE_DEFAULT;                      // Default usage for GPU access
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;             // Bind as depth-stencil buffer

        // Create the new depth stencil buffer
        hr = m_d3dDevice->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
        if (FAILED(hr)) {
            // Log error if creating depth stencil buffer fails
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create depth stencil buffer after resize");
            // Clear resizing and transition flags on failure
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Create the depth stencil view
        hr = m_d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());
        if (FAILED(hr)) {
            // Log error if creating depth stencil view fails
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create depth stencil view after resize");
            // Clear resizing and transition flags on failure
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Set the viewport for exclusive fullscreen with the actual resolution achieved
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(closestMode.Width);           // Viewport width from closest mode
        vp.Height = static_cast<float>(closestMode.Height);         // Viewport height from closest mode
        vp.MinDepth = 0.0f;                                         // Minimum depth value
        vp.MaxDepth = 1.0f;                                         // Maximum depth value
        vp.TopLeftX = 0;                                            // Top-left X coordinate
        vp.TopLeftY = 0;                                            // Top-left Y coordinate
        m_d3dContext->RSSetViewports(1, &vp);                       // Set the viewport in the rendering context

        // Bind the new render target and depth stencil to the output merger stage
        m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

        // Update rendering dimensions to reflect the actual resolution achieved
        iOrigWidth = closestMode.Width;                             // Update internal width tracking
        iOrigHeight = closestMode.Height;                           // Update internal height tracking

        // Recreate Direct2D resources for the new resolution
        CreateDirect2DResources();

        // Clear all transition and resizing flags to indicate completion
        threadManager.threadVars.bIsResizing.store(false);
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

#if defined(_DEBUG_RENDERER_)
        // Log successful completion with actual resolution achieved
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[RENDERER] Exclusive fullscreen mode set successfully at %dx%d", closestMode.Width, closestMode.Height);
#endif

        // Return success
        return true;
    }
    catch (const std::exception& e) {
        // Log exception details for debugging purposes
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[RENDERER] Exception in SetFullExclusive: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));

        // Clear all flags on exception to prevent deadlock
        threadManager.threadVars.bIsResizing.store(false);
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        return false;   // Return failure
    }
}

bool DX11Renderer::SetWindowedScreen(void)
{
#if defined(_DEBUG_RENDERER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] SetWindowedScreen() called - beginning windowed transition");
#endif

    // Check if already in fullscreen transition to prevent race conditions
    if (bFullScreenTransition.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] Fullscreen transition already in progress");
        return false;
    }

    // Set fullscreen transition flag to block other operations
    bFullScreenTransition.store(true);
    threadManager.threadVars.bSettingFullScreen.store(true);

    try {
        std::lock_guard<std::mutex> lock(s_renderMutex);                        // Ensure thread safety during windowed transition

        // Set to windowed mode first
        HRESULT hr = m_swapChain->SetFullscreenState(FALSE, nullptr);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to set windowed state");
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

#if defined(_DEBUG_RENDERER_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERER] Target windowed resolution: " +
            std::to_wstring(windowedWidth) + L"x" + std::to_wstring(windowedHeight));
#endif

        // Set resizing flag to prevent other operations during buffer resize
        threadManager.threadVars.bIsResizing.store(true);

        // Clean up resources before resize
        if (m_d2dContext) {
            m_d2dContext->SetTarget(nullptr);                                   // Clear D2D render target
            m_d2dContext->Flush();                                              // Flush any pending D2D operations
            D2DBusy.store(false);                                               // Mark D2D as not busy
        }

        // Release render target views and other resources
        m_d2dRenderTarget.Reset();                                              // Release D2D render target
        m_d2dContext.Reset();                                                   // Release D2D context
        dxgiSurface.Reset();                                                    // Release DXGI surface
        Clean2DTextures();                                                      // Clean up 2D textures
        m_renderTargetView.Reset();                                             // Release 3D render target
        m_depthStencilView.Reset();                                             // Release depth stencil view
        m_depthStencilBuffer.Reset();                                           // Release depth stencil buffer

        // Resize the buffers to windowed resolution
        hr = m_swapChain->ResizeBuffers(
            0,                                                                  // Keep existing buffer count
            windowedWidth,                                                      // New width
            windowedHeight,                                                     // New height
            DXGI_FORMAT_UNKNOWN,                                                // Keep existing format
            0                                                                   // Default flags
        );

        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to resize buffers for windowed mode");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Recreate render target view
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to get back buffer after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create render target view after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Recreate depth buffer
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = windowedWidth;                                        // Set depth buffer width
        depthDesc.Height = windowedHeight;                                      // Set depth buffer height
        depthDesc.MipLevels = 1;                                                // Single mip level
        depthDesc.ArraySize = 1;                                                // Single array element
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;                      // 24-bit depth, 8-bit stencil
        depthDesc.SampleDesc.Count = 1;                                         // No multisampling
        depthDesc.Usage = D3D11_USAGE_DEFAULT;                                  // Default usage
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;                         // Bind as depth-stencil

        hr = m_d3dDevice->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create depth stencil buffer after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        hr = m_d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to create depth stencil view after resize");
            threadManager.threadVars.bIsResizing.store(false);
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return false;
        }

        // Set the viewport for windowed mode
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(windowedWidth);                           // Viewport width
        vp.Height = static_cast<float>(windowedHeight);                         // Viewport height
        vp.MinDepth = 0.0f;                                                     // Minimum depth
        vp.MaxDepth = 1.0f;                                                     // Maximum depth
        m_d3dContext->RSSetViewports(1, &vp);                                   // Set the viewport

        // Bind the new render target and depth stencil
        m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

        // Update rendering dimensions
        iOrigWidth = windowedWidth;                                             // Update width
        iOrigHeight = windowedHeight;                                           // Update height

        // Recreate D2D resources
        CreateDirect2DResources();

        // Clear all flags
        threadManager.threadVars.bIsResizing.store(false);
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        // Reset window size and position to center it on screen
        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        int centerX = (workArea.right - workArea.left - windowedWidth) / 2;
        int centerY = (workArea.bottom - workArea.top - windowedHeight) / 2;

        SetWindowPos(hwnd, nullptr, centerX, centerY, windowedWidth, windowedHeight, SWP_NOZORDER);

        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] Windowed mode set successfully");

        return true;
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Exception in SetWindowedScreen: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));

        // Clear all flags on exception
        threadManager.threadVars.bIsResizing.store(false);
        bFullScreenTransition.store(false);
        threadManager.threadVars.bSettingFullScreen.store(false);

        return false;
    }
}

#if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
void DX11Renderer::SetDebugMode(int mode)
{
    DebugBuffer dbg = {};
    dbg.debugMode = mode;

    D3D11_MAPPED_SUBRESOURCE mappedDbg;
    if (SUCCEEDED(m_d3dContext->Map(m_debugBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedDbg))) {
        memcpy(mappedDbg.pData, &dbg, sizeof(DebugBuffer));
        m_d3dContext->Unmap(m_debugBuffer.Get(), 0);

        m_d3dContext->PSSetConstantBuffers(SLOT_DEBUG_BUFFER, 1, m_debugBuffer.GetAddressOf());
    }
}
#endif

#if defined(_DEBUG_RENDERER_) && defined(_SIMPLE_TRIANGLE_)
// Minimal DX11 triangle test - no shaders, no instances, just raw geometry
struct SimpleVertex {
    XMFLOAT3 pos;
};

void DX11Renderer::TestDrawTriangle()
{
    static ComPtr<ID3D11Buffer> testVB;
    static ComPtr<ID3D11VertexShader> testVS;
    static ComPtr<ID3D11PixelShader> testPS;
    static ComPtr<ID3D11InputLayout> inputLayout;

    if (!testVB)
    {
        // Triangle vertices
        SimpleVertex verts[] = {
            { XMFLOAT3(0.0f,  0.5f, 0.0f) },
            { XMFLOAT3(0.5f, -0.5f, 0.0f) },
            { XMFLOAT3(-0.5f, -0.5f, 0.0f) },
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.ByteWidth = sizeof(verts);
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData = { verts };
        m_d3dDevice->CreateBuffer(&vbDesc, &vbData, &testVB);

        // Simple passthrough shaders
        const char* vsCode =
            "struct VSInput { float3 pos : POSITION; };\n"
            "struct VSOutput { float4 pos : SV_POSITION; };\n"
            "VSOutput main(VSInput input) {\n"
            "    VSOutput o;\n"
            "    o.pos = float4(input.pos, 1.0f);\n"
            "    return o;\n"
            "}";

        const char* psCode =
            "float4 main() : SV_TARGET { return float4(1, 0, 1, 1); }";

        ComPtr<ID3DBlob> vsBlob, psBlob, errors;
        D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errors);
        m_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &testVS);

        D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errors);
        m_d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &testPS);

        // Input layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        m_d3dDevice->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    }

    // Bind pipeline
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_d3dContext->IASetVertexBuffers(0, 1, testVB.GetAddressOf(), &stride, &offset);
    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->IASetInputLayout(inputLayout.Get());
    m_d3dContext->VSSetShader(testVS.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(testPS.Get(), nullptr, 0);

    // Draw one triangle
    m_d3dContext->Draw(3, 0);
}
#endif

// *-------------------------------------------------------------------------------------*
// This PRIVATE function will select the best GPU Based Adapter available on the system.
// *-------------------------------------------------------------------------------------*
ComPtr<IDXGIAdapter1> DX11Renderer::SelectBestAdapter()
{
    // Get the window's position
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    POINT centerPoint = {
        (windowRect.left + windowRect.right) / 2,
        (windowRect.top + windowRect.bottom) / 2
    };

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: Failed to create DXGI Factory.");
        return nullptr;
    }

    ComPtr<IDXGIAdapter1> bestAdapter = nullptr;
    UINT bestScore = 0;
    UINT index = 0;

    while (true) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index++, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        std::wstring name(desc.Description);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Found Adapter: " + name);

        // Check if this adapter controls the display where the window is
        UINT outputIndex = 0;
        ComPtr<IDXGIOutput> output;
        bool controlsWindow = false;

        while (adapter->EnumOutputs(outputIndex++, &output) != DXGI_ERROR_NOT_FOUND) {
            DXGI_OUTPUT_DESC outputDesc;
            output->GetDesc(&outputDesc);

            RECT monitorRect = outputDesc.DesktopCoordinates;
            if (centerPoint.x >= monitorRect.left && centerPoint.x <= monitorRect.right &&
                centerPoint.y >= monitorRect.top && centerPoint.y <= monitorRect.bottom) {
                // Give priority to adapters that control the window's display
                controlsWindow = true;
                break;
            }

            output.Reset();
        }

        // Score calculation
        UINT score = 0;
        if (controlsWindow) score += 10000;  // Huge priority for adapters controlling the window
        if (desc.VendorId == 0x10DE) score += 1000; // NVIDIA
        if (desc.VendorId == 0x1002) score += 900;  // AMD
        if (desc.VendorId == 0x8086) score += 100;  // Intel

        score += desc.DedicatedVideoMemory / (1024 * 1024); // more VRAM = better

        if (score > bestScore) {
            bestScore = score;
            bestAdapter = adapter;
        }
    }

    if (bestAdapter) {
        DXGI_ADAPTER_DESC1 desc;
        bestAdapter->GetDesc1(&desc);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Using Adapter: " + std::wstring(desc.Description));
    }
    else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"No suitable GPU adapter found.");
    }

    return bestAdapter;
}

void* DX11Renderer::GetDevice()
{
    // Return the raw pointer from ComPtr - can be cast back to ComPtr<ID3D11Device>
    // in calling code using: ComPtr<ID3D11Device> device; device.Attach(static_cast<ID3D11Device*>(renderer->GetDevice()));
    return m_d3dDevice.Get();
}

void* DX11Renderer::GetDeviceContext()
{
    // Return the raw pointer from ComPtr - can be cast back to ComPtr<ID3D11DeviceContext>
    // in calling code using: ComPtr<ID3D11DeviceContext> context; context.Attach(static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext()));
    return m_d3dContext.Get();
}

void* DX11Renderer::GetSwapChain()
{
    // Return the raw pointer from ComPtr - can be cast back to ComPtr<IDXGISwapChain>
    // in calling code using: ComPtr<IDXGISwapChain> swapChain; swapChain.Attach(static_cast<IDXGISwapChain*>(renderer->GetSwapChain()));
    return m_swapChain.Get();
}
/* -------------------------------------------------------------- */
// Main Tasking Thread for our renderer
//
// This function is the main (optional thread) function that will 
// be used to render the scene. It will be responsible for rendering 
// the scene, updating the scene, and handling any other rendering 
// tasks.
//
// --- Begin 3D Rendering ---
// --- Begin 2D Rendering ---
// --- Post-process FX ---
// --- Present to screen ---

// NOTE: Please leave this function on the bottom of this file
// ===== As locating it is easier for debugging & writing purposes.
/* -------------------------------------------------------------- */
void DX11Renderer::RenderFrame()
{
    // SAFE-GUARDS
    if (bHasCleanedUp || !m_d3dDevice || !m_d3dContext || !m_cameraConstantBuffer) return;
    if (threadManager.threadVars.bIsShuttingDown.load() || bIsMinimized.load() || threadManager.threadVars.bIsResizing.load() || 
        threadManager.threadVars.bIsRendering.load() || !bIsInitialized.load()) return;

    try
	{
        // Try to acquire the render frame lock with a 10ms timeout
        if (!threadManager.TryLock(renderFrameLockName, 10)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire render frame lock - timeout reached");
            return;
        }

        // Clear buffers & Initialize
        HRESULT hr = S_OK;
        HWND hWnd = hwnd;

        // Save the current Direct3D state
		ComPtr<ID3D11RenderTargetView> previousRenderTargetView;
		ComPtr<ID3D11DepthStencilView> previousDepthStencilView;
		m_d3dContext->OMGetRenderTargets(1, previousRenderTargetView.GetAddressOf(), previousDepthStencilView.GetAddressOf());

		D3D11_VIEWPORT previousViewport;
		UINT numViewports = 1;
		m_d3dContext->RSGetViewports(&numViewports, &previousViewport);

		FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

        D3D11_VIEWPORT viewport = {};
		RECT rc;
		POINT cursorPos;
        if (!winMetrics.isFullScreen)
        {
            GetClientRect(hWnd, &rc);
        }
        else
        {
            rc = winMetrics.monitorFullArea;
        }

		float width = float(rc.right - rc.left);
		float height = float(rc.bottom - rc.top);
        viewport.Width = width;
		viewport.Height = height;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		m_d3dContext->RSSetViewports(1, &viewport);

        static Microsoft::WRL::ComPtr<ID3D11RasterizerState> wireframeRS;

        #if defined(_DEBUG_RENDER_WIREFRAME_)
            if (bWireframeMode && m_wireframeState)
            {
                // Set rasterizer state
                m_d3dContext->RSSetState(m_wireframeState.Get());
            }
            else if (m_rasterizerState)
            {
                // Set rasterizer state
                m_d3dContext->RSSetState(m_rasterizerState.Get());
            }
        #else
            // DEFAULT: Set rasterizer state
            m_d3dContext->RSSetState(m_rasterizerState.Get());
        #endif

        // Check the status of the rendering thread
#ifdef RENDERER_IS_THREAD
        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
		while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) && (!threadManager.threadVars.bIsShuttingDown.load()))
		{
			status = threadManager.GetThreadStatus(THREAD_RENDERER);
			if (status == ThreadStatus::Paused)
			{
				Sleep(1);
				continue;
			}
#endif

            if (m_d3dDevice)
            {
                HRESULT hr = m_d3dDevice->GetDeviceRemovedReason();

                // Only attempt reset if:
                // 1. Device is reporting failure
                // 2. Thread is NOT actively resizing
                // 3. Window is not minimized or hidden
                if (FAILED(hr) && (!threadManager.threadVars.bIsResizing.load()))
                {
                    if (!sysUtils.IsWindowMinimized()) // <- Confirm window state first!
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX11Renderer] Device removed detected. Attempting Reset.");
                        threadManager.threadVars.bIsResizing.store(true);
                        Resize(iOrigWidth, iOrigHeight);
                        ResumeLoader();
                        threadManager.threadVars.bIsResizing.store(false);
                        return;
                    }
                    else
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX11Renderer] Device removed but window minimized. Skipping reset.");
                    }
                }
            }

			// State that we are now rendering
            threadManager.threadVars.bIsRendering.store(true);
            
            // Clear the render target and depth stencil view before we start rendering
			if (m_d3dContext)
			{
                m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
                m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            }

            // Update the cameras viewmatrix
			myCamera.UpdateViewMatrix();

            // Update effects
            static auto myLastTime = std::chrono::high_resolution_clock::now();
            auto myCurrentTime = std::chrono::high_resolution_clock::now();
            myLastTime = myCurrentTime;

            // Get current frame start time
            auto now = std::chrono::steady_clock::now();

            // Compute delta time in seconds as a float
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();

            // Update the timer for the next frame
            lastFrameTime = now;

            GetCursorPos(&cursorPos);
            ScreenToClient(hWnd, &cursorPos);
            myMouseCoords.x = cursorPos.x;
            myMouseCoords.y = cursorPos.y;
            // Scale the mouse coordinates
            auto [scaledX, scaledY] = sysUtils.ScaleMouseCoordinates(cursorPos.x, cursorPos.x, iOrigWidth, iOrigHeight, width, height);
            float x = float(scaledX);
			float y = float(scaledY);

            // We now need to determine which Scene we are to render for?
            m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
            switch (scene.stSceneType)
            {
                case SceneType::SCENE_SPLASH:
                {
                    // Try to acquire the D2D render lock with a 100ms timeout
                    if (threadManager.TryLock(D2DLockName, 100))                        // Signal that Direct2D is now busy with rendering operations.
                    {
                        // Inform Direct2D we are to begin drawing calls.
                        m_d2dRenderTarget->BeginDraw();

                        // Present Splash Screen
                        if ((m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)]))
                            Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);

                        m_d2dRenderTarget->EndDraw();
                        // Release the D2D render lock
                        threadManager.RemoveLock(D2DLockName);
                    }
                    else 
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D render lock - skipping D2D operations");
                    }

                    break;
                }

                case SceneType::SCENE_INTRO_MOVIE:
                {
                    // Try to acquire the D2D render lock with a 100ms timeout
                    if (threadManager.TryLock(D2DLockName, 100))                        // Signal that Direct2D is now busy with rendering operations.
                    {
                        // Inform Direct2D we are to begin drawing calls.
                        m_d2dRenderTarget->BeginDraw();
                        // Is the movie playing
                        if (moviePlayer.IsPlaying()) {
                            // Update the movie frame
                            moviePlayer.UpdateFrame();

                            // Render the movie to fill the screen
                            moviePlayer.Render(Vector2(0, 0), Vector2(iOrigWidth, iOrigHeight));

                            // Check to see if the user has pressed the space bar
                            if (GetAsyncKeyState(' ') & 0x8000)
                            {
                                // Stop Playback as this will switch the scene!
                                moviePlayer.Stop(); 
                            }
                        }

                        m_d2dRenderTarget->EndDraw();
                        // Release the D2D render lock
                        threadManager.RemoveLock(D2DLockName);
                    }
                    else
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D render lock - skipping D2D operations");
                    }

                    break;
                }

                case SceneType::SCENE_GAMEPLAY:
                {
                    // Clear buffers
                    if (m_d3dContext)
                    {
                        // 3D Rendering

                        // Populate the constant buffer data
                        ConstantBuffer cb;
                        cb.viewMatrix = myCamera.GetViewMatrix();
                        cb.projectionMatrix = myCamera.GetProjectionMatrix();
                        cb.cameraPosition = myCamera.GetPosition();

                        // Update the constant buffer
                        D3D11_MAPPED_SUBRESOURCE mappedResource;
                        HRESULT hr = m_d3dContext->Map(m_cameraConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
                        if (FAILED(hr)) {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateConstantBuffer: Failed to map constant buffer.");
                            return;
                        }

                        // Copy the data to the constant buffer
                        memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer));
                        m_d3dContext->Unmap(m_cameraConstantBuffer.Get(), 0);
                        // Bind the constant buffer to the vertex shader
                        m_d3dContext->VSSetConstantBuffers(SLOT_CONST_BUFFER, 1, m_cameraConstantBuffer.GetAddressOf());

                        // Debug constant buffer for debugging pixel shader.
                        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
                            if (GetAsyncKeyState('1') & 0x8000) SetDebugMode(0); // Run as Normally would! Production view!
                            if (GetAsyncKeyState('2') & 0x8000) SetDebugMode(1); // Normals Only
                            if (GetAsyncKeyState('3') & 0x8000) SetDebugMode(2); // Texture Only
                            if (GetAsyncKeyState('4') & 0x8000) SetDebugMode(3); // Lighting Only
                            if (GetAsyncKeyState('5') & 0x8000) SetDebugMode(4); // Specular Only
                            if (GetAsyncKeyState('6') & 0x8000) SetDebugMode(5); // Attenuation/Normals
                            if (GetAsyncKeyState('7') & 0x8000) SetDebugMode(6); // Shadows Only
                            if (GetAsyncKeyState('8') & 0x8000) SetDebugMode(7); // Reflection Only
                            if (GetAsyncKeyState('9') & 0x8000) SetDebugMode(8); // Metallic Only
                        #endif

                        // Ensure all loading is complete before rendering models and lighting.
                        if (threadManager.threadVars.bLoaderTaskFinished.load())
                        {
                            // (Add 3D object rendering here)
                            #if defined(_RENDERER_WIREFRAME_)
                                m_d3dContext->RSSetState(wireframeRS.Get());
                            #endif

                            // This block is used if you need to test your Pipeline.
                            #if defined(_DEBUG_RENDERER_) && defined(_SIMPLE_TRIANGLE_) && defined(_DEBUG)
                                TestDrawTriangle();                                                                                     // Test triangle for rendering (debug purposes)
                            #endif

                            for (int i = 0; i < MAX_MODELS; ++i)
                            {
                                if (scene.scene_models[i].m_isLoaded)
                                {
                                    scene.scene_models[i].m_modelInfo.fxActive = false;                                                 // Force this off before render call for now.
                                    scene.scene_models[i].m_modelInfo.viewMatrix = myCamera.GetViewMatrix();
                                    scene.scene_models[i].m_modelInfo.projectionMatrix = myCamera.GetProjectionMatrix();
                                    scene.scene_models[i].m_modelInfo.cameraPosition = myCamera.GetPosition();
                                    scene.scene_models[i].UpdateAnimation(deltaTime);                                                   // Update animation for this model
                                    scene.scene_models[i].Render(m_d3dContext.Get(), deltaTime);
                                }
                            }
                        }

                        // Animate Lights Per Frame
        //                    lightsManager.AnimateLights(deltaTime); // TODO: Replace with real deltaTime

                        // Update the General Lighting, not object lighting as
                        // this is handle elsewhere (ie. Model::Render())
                        std::vector<LightStruct> globalLights = lightsManager.GetAllLights();

                        GlobalLightBuffer glb = {};
                        glb.numLights = static_cast<int>(globalLights.size());
                        if (glb.numLights > MAX_GLOBAL_LIGHTS)
                            glb.numLights = MAX_GLOBAL_LIGHTS;

                        for (int i = 0; i < glb.numLights; ++i)
                        {
                            //                    globalLights[i].intensity = 1.0f;
                            //                    globalLights[i].baseIntensity = 1.2f;
                            memcpy(&glb.lights[i], &globalLights[i], sizeof(LightStruct)); // Copy the light data
                            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG_LIGHTING_)
                                debug.logDebugMessage(LogLevel::LOG_INFO, L"Global Lights: %d", glb.numLights);
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"Global Light[%zu] active=%d intensity=%.2f color=(%.2f %.2f %.2f) range=%.2f type=%d position=(%.2f, %.2f, %.2f)",
                                    i, glb.lights[i].active, glb.lights[i].intensity,
                                    glb.lights[i].color.x, glb.lights[i].color.y, glb.lights[i].color.z,
                                    glb.lights[i].range, glb.lights[i].type,
                                    glb.lights[i].position.x, glb.lights[i].position.y, glb.lights[i].position.z);
                            #endif
                        }

                        // Upload to GPU
                        D3D11_MAPPED_SUBRESOURCE mapped;
                        hr = m_d3dContext->Map(m_globalLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                        if (SUCCEEDED(hr)) {
                            memcpy(mapped.pData, &glb, sizeof(GlobalLightBuffer));
                            m_d3dContext->Unmap(m_globalLightBuffer.Get(), 0);
                        }

                        // Bind to PS slot b3
                        m_d3dContext->PSSetConstantBuffers(SLOT_GLOBAL_LIGHT_BUFFER, 1, m_globalLightBuffer.GetAddressOf());

                    } // End of 3D Rendering

                    break;
                }
            }
			
            // 2D Rendering
            if (m_d2dRenderTarget)
            {
                // Check status of Direct2D to see if available for use.
                if (threadManager.TryLock(D2DLockName, 100))                        // Signal that Direct2D is now busy with rendering operations.
                {
                    // Inform Direct2D we are to begin drawing calls.
                    m_d2dRenderTarget->BeginDraw();

                    // (Add 2D UI/text rendering here)
/*
                    for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
                    {
                        for (int iPhase = int(BlitPhaseLevel::PHASE_LEVEL_1); iPhase <= int(BlitPhaseLevel::PHASE_LEVEL_5); iPhase++)
                        {
                            if (My2DBlitQueue[iX].bInUse && int(My2DBlitQueue[iX].BlitPhase) == iPhase)
                            {
                                Blit2DObject(My2DBlitQueue[iX].BlitObjDetails.iBlitID, My2DBlitQueue[iX].BlitObjDetails.iBlitX, My2DBlitQueue[iX].BlitObjDetails.iBlitY);
                            }
                        }
                    }
*/
                    if ((!threadManager.threadVars.bIsShuttingDown.load()) && 
                        (!bIsMinimized.load()) && 
                        (!threadManager.threadVars.bIsResizing.load()) && 
                        (bIsInitialized.load()))
                    {
                        switch (scene.stSceneType)
                        {
                            case SceneType::SCENE_INTRO:
                            {
                                // Ensure all loading is complete before rendering models and lighting.
                                if (threadManager.threadVars.bLoaderTaskFinished.load())
                                {
                                    myCamera.SetYawPitch(0.285f, -0.22f);
                                    // Draw background image first.
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                                    // Draw Logo image.
                                    Blit2DObject(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - 47);
                                    // Render our 3D starfield.
                                    fxManager.RenderFX(fxManager.starfieldID, m_d3dContext.Get(), myCamera.GetViewMatrix());
                                    break;
                                }
                            }

                            case SceneType::SCENE_INTRO_MOVIE:
                            {
                                if (moviePlayer.IsPlaying() && (!threadManager.threadVars.bLoaderTaskFinished.load()))
                                {
                                    // Draw Logo image.
                                    Blit2DObject(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - 47);
                                }
                                break;
                            }
                        }
                    }

                    // Are we to display the FPS so we can see 
                    // our render frame rate (Handy for debugging/optimization requirements)
                    if (USE_FPS_DISPLAY)
                    {
                        static auto lastFrameTime = std::chrono::steady_clock::now();
                        static auto lastFPSTime = lastFrameTime;
                        static int frameCounter = 0;

                        auto currentTime = std::chrono::steady_clock::now();
                        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();  // Frame delta time
                        float elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count();

                        lastFrameTime = currentTime;
                        frameCounter++;

                        if (elapsedForFPS >= 1.0f)
                        {
                            fps = static_cast<float>(frameCounter) / elapsedForFPS;
                            frameCounter = 0;
                            lastFPSTime = currentTime;
                        }

                        XMFLOAT3 Coords;
                        Coords = myCamera.GetPosition();
                        std::wstring fpsText = L"FPS: " + std::to_wstring(fps) + L"\nMOUSE: x" + std::to_wstring(cursorPos.x) + L", y" + std::to_wstring(cursorPos.y);
                        fpsText = fpsText + L"\nCamera X: " + std::to_wstring(Coords.x) + L", Y: " + std::to_wstring(Coords.y) + L", Z: " + std::to_wstring(Coords.z) +
                            L", Yaw: " + std::to_wstring(myCamera.m_yaw) + L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n";
						fpsText = fpsText + L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n";

                        DrawMyText(fpsText, Vector2(0, 0), MyColor(255, 255, 255, 255), 10.0f);
                    }

                    if (!threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        delay++;
                        if (delay > 5)
                        {
                            loadIndex++;
                            if (loadIndex > 9) { loadIndex = 0; }
                            delay = 0;
                        }

                        if ((m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)]))
                        {
                            iPosX = loadIndex << 5;
                            Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE, width - 32, height - 32, iPosX, 0, 32, 32);
                        }
                    }

                    // Apply 2D Effects.
                    fxManager.Render2D();
                    // Now render any windows that are to be displayed.
                    guiManager.Render();

                    // Now Render the Mouse Cursor
                    if ((m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)]))
                        Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR, cursorPos.x, cursorPos.y);

                    // State we are NOW finished With Direct2D Drawing.
                    try
                    {
                        HRESULT hr = m_d2dRenderTarget->EndDraw();
                        if (FAILED(hr))
                        {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Direct2D EndDraw failed.");
                        }

                        // Render effects (Fader) (should be after normal rendering but before present)
                        fxManager.Render();
                    }
                    catch (const std::exception& e)
                    {}

                    // Release the D2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D render lock - skipping D2D operations");
                }
            }

            // Present frame
//			WaitForGPUToFinish();
            #if defined(_DEBUG_RENDERER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Presenting frame...");
            #endif
            if (m_swapChain) { m_swapChain->Present(config.myConfig.enableVSync ? 1 : 0, 0); }

            // State that we are no longer rendering    
            threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
        }
#endif
        // Make sure to remove the lock even if an exception occurs
        threadManager.RemoveLock(renderFrameLockName);
    }
    catch (const std::exception& e)
    {
        // Make sure to remove the lock even if an exception occurs
        threadManager.RemoveLock(renderFrameLockName);
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[RENDER] - " + std::wstring(e.what(), e.what() + strlen(e.what())));
    }

    #ifdef RENDERER_IS_THREAD
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Render Thread Exiting.");
    #endif
}

#endif