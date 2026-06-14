#define NOMINMAX

#include "Includes.h"

// Perform Renderer to USE Test.
// This is done to ensure we only include required code.
// Meaning, if we are NOT using this Renderer, forget it
// and DO NOT include its code.
#if defined(__USE_DIRECTX_11__)
    // DirectX 11 Required Headers & Linking
    #include "Renderer.h"

    #include "DX11Renderer.h"
    #include "Debug.h"
    #include "ExceptionHandler.h"
    #include "WinSystem.h"
    #include "Configuration.h"
    #include "DX_FXManager.h"
    #include "GUIManager.h"
    #include "Models.h"
    #include "Lights.h"
    #include "SceneManager.h"
    #include "ShaderManager.h"

    #if defined(__USE_MP3PLAYER__)
        #include "WinMediaPlayer.h"
    #elif defined(__USE_XMPLAYER__)
        #include "XMMODPlayer.h"
    #endif

    #include <d3dcompiler.h>

    #pragma comment(lib, "d3dcompiler.lib")
    #pragma comment(lib, "dxgi.lib")

    #pragma warning(push)
    #pragma warning(disable: 4101)

// Static member initialization
std::mutex DX11Renderer::s_renderMutex;

extern HWND hwnd;
extern HINSTANCE hInst;
extern GUIManager guiManager;
extern Debug debug;
extern ExceptionHandler exceptionHandler;
extern SystemUtils sysUtils;
extern SceneManager scene;
extern ThreadManager threadManager;
extern ShaderManager shadeManager;
extern FXManager fxManager;
extern Vector2 myMouseCoords;
extern Model models[MAX_MODELS];
extern LightsManager lightsManager;
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
    
    // Seed render-target dimensions from the loaded configuration so that
    // any internal resource creation that runs before CreateRenderTargetViews()
    // has a valid configured resolution rather than a hardcoded fallback.
    m_renderTargetWidth  = config.myConfig.resolutionWidth;
    m_renderTargetHeight = config.myConfig.resolutionHeight;

    iOrigWidth  = winMetrics.clientWidth;
    iOrigHeight = winMetrics.clientHeight;

    // Initilize Direct2D & Direct3D 11 Device and Swap Chain
    CreateDeviceAndSwapChain(hwnd);
    CreateDirect2DResources();
    CreateRenderTargetViews();
    CreateDepthStencilBuffer();
    SetupViewport();
    SetupPipelineStates();

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

    // Create Global Light Buffer.
    // The runtime-compiled ModelPixel shader requires at least 1728 bytes for b3
    // (same constraint as the per-model b1 buffer).  sizeof(GlobalLightBuffer) is
    // only 1296 bytes, which triggers DEVICE_DRAW_CONSTANT_BUFFER_TOO_SMALL and
    // causes the shader to read zeros for all light data → grey ship.
    static const UINT kGlobalLightCBMinBytes = 1728;
    const UINT globalLightCBBytes = sizeof(GlobalLightBuffer) > kGlobalLightCBMinBytes
        ? ((static_cast<UINT>(sizeof(GlobalLightBuffer)) + 15u) & ~15u)
        : kGlobalLightCBMinBytes;

    D3D11_BUFFER_DESC lightCBDesc = {};
    lightCBDesc.ByteWidth = globalLightCBBytes;
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

    // Create persistent dynamic quad vertex buffers so DrawTexture/DrawRectangle
    // don't call CreateBuffer every frame — we Map/Unmap these instead.
    {
        D3D11_BUFFER_DESC qvb = {};
        qvb.Usage          = D3D11_USAGE_DYNAMIC;
        qvb.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        qvb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        qvb.ByteWidth      = 80;   // sizeof({XMFLOAT3,XMFLOAT2}) * 4
        m_d3dDevice->CreateBuffer(&qvb, nullptr, m_quadTexVB.GetAddressOf());
        qvb.ByteWidth      = 112;  // sizeof({XMFLOAT3,XMFLOAT4}) * 4
        m_d3dDevice->CreateBuffer(&qvb, nullptr, m_quadRectVB.GetAddressOf());
    }

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
        // Pre-load cursor and loader-ring before the loader thread runs so
        // the ring is visible immediately during the first load.
        const int cursorIdx = int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR);
        const int ringIdx   = int(BlitObj2DIndexType::BG_LOADER_CIRCLE);
        LoadTexture(cursorIdx, AssetsDir / texFilename[cursorIdx], true);
        LoadTexture(ringIdx,   AssetsDir / texFilename[ringIdx],   true);

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

IDWriteTextFormat* DX11Renderer::GetOrCreateTextFormat(const wchar_t* fontName, float fontSize,
                                                        DWRITE_FONT_WEIGHT weight)
{
    if (!m_dwriteFactory) return nullptr;
    TextFormatKey key{ fontName, fontSize, weight };
    auto it = m_textFormatCache.find(key);
    if (it != m_textFormatCache.end()) return it->second.Get();

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        fontName, nullptr, weight,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &fmt);
    if (FAILED(hr)) return nullptr;

    IDWriteTextFormat* raw = fmt.Get();
    m_textFormatCache.emplace(std::move(key), std::move(fmt));
    return raw;
}

void DX11Renderer::InvalidateTextFormatCache()
{
    m_textFormatCache.clear();
}

void DX11Renderer::Clear2DBlitQueue()
{
    SecureZeroMemory(My2DBlitQueue, sizeof(My2DBlitQueue));
    m_blitActiveIDs.clear();
}

bool DX11Renderer::Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl, BlitObj2DType objType, BlitObj2DDetails objDetails, CanBlitType BlitType)
{
    // O(1) duplicate check via hash set (replaces the old O(N) linear scan)
    if (BlitType == CanBlitType::CAN_BLIT_SINGLE)
    {
        if (m_blitActiveIDs.count(int(iIndex)))
            return FALSE;
    }

    // Find an empty slot in the queue
    for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
    {
        if (!My2DBlitQueue[iX].bInUse)
        {
            My2DBlitQueue[iX].bInUse = TRUE;
            My2DBlitQueue[iX].BlitPhase = BlitPhaseLvl;
            My2DBlitQueue[iX].BlitObjType = objType;
            My2DBlitQueue[iX].BlitObjDetails = objDetails;
            My2DBlitQueue[iX].BlitObjDetails.iBlitID = iIndex;
            m_blitActiveIDs.insert(int(iIndex));
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

    // Snapshot ComPtrs to hold ref counts — prevents dangling pointer if resize
    // thread calls Clean2DTextures() concurrently between the null check and DrawBitmap.
    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[int(iIndex)];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;

    if (!bitmap || !rt)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Invalid texture or render target in Blit2DObject()");
        return;
    }

    D2D1_SIZE_F bitmapSize = bitmap->GetSize();

    D2D1_RECT_F destRect = D2D1::RectF(
        static_cast<float>(iX),
        static_cast<float>(iY),
        static_cast<float>(iX + iWidth),
        static_cast<float>(iY + iHeight)
    );

    D2D1_RECT_F srcRect = D2D1::RectF(
        0.0f,
        0.0f,
        bitmapSize.width,
        bitmapSize.height
    );

    rt->DrawBitmap(bitmap.Get(), destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
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

    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[int(iIndex)];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;

    if (!bitmap || !rt)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Invalid texture or render target in Blit2DObject()");
        return;
    }

    D2D1_SIZE_F bitmapSize = bitmap->GetSize();

    D2D1_RECT_F destRect = D2D1::RectF(
        static_cast<float>(iX),
        static_cast<float>(iY),
        static_cast<float>(iX) + bitmapSize.width,
        static_cast<float>(iY) + bitmapSize.height
    );

    D2D1_RECT_F srcRect = D2D1::RectF(0.0f, 0.0f, bitmapSize.width, bitmapSize.height);

    rt->DrawBitmap(bitmap.Get(), destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
}

//-------------------------------------------------------------------------------------------------
// Blit2DColoredPixel - Renders a single 1x1 pixel at (x, y) using the specified RGBA color.
// Utilizes Direct2D render target for immediate 2D pixel output.
//-------------------------------------------------------------------------------------------------
void DX11Renderer::Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color)
{
    if (!m_d2dRenderTarget || threadManager.threadVars.bIsResizing.load()) return;

    // m_pixelBrush is a member reset during D2D cleanup (Resize/Cleanup), so it is
    // always bound to the current render target's factory — no aliasing risk.
    if (!m_pixelBrush)
    {
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(color.x, color.y, color.z, color.w), &m_pixelBrush);
    }
    else
    {
        m_pixelBrush->SetColor(D2D1::ColorF(color.x, color.y, color.z, color.w));
    }

    D2D1_RECT_F pixelRect = D2D1::RectF((FLOAT)x, (FLOAT)y, (FLOAT)x + pixelSize, (FLOAT)y + pixelSize);
    m_d2dRenderTarget->FillRectangle(&pixelRect, m_pixelBrush.Get());
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

    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[int(iIndex)];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;

    if (!bitmap || !rt)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Invalid texture or render target in Blit2DObject()");
        return;
    }

    D2D1_SIZE_F bitmapSize = bitmap->GetSize();

    D2D1_RECT_F destRect = D2D1::RectF(
        static_cast<float>(iBlitX),
        static_cast<float>(iBlitY),
        static_cast<float>(iBlitX + iTileSizeX),
        static_cast<float>(iBlitY + iTileSizeY)
    );

    float fXOffset = float(iXOffset);
    float fYOffset = float(iYOffset);
    D2D1_RECT_F srcRect = D2D1::RectF(
        fXOffset,
        fYOffset,
        static_cast<float>(fXOffset + iTileSizeX),
        static_cast<float>(fYOffset + iTileSizeY)
    );

    rt->DrawBitmap(bitmap.Get(), destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
}

// Blit2DCenteredZoom — renders a 2D image with a center-cropped zoom.
// At zoomFactor 0.0 the full image is shown; at 0.75 only the centre 25% (per axis) is shown, scaled to fill the dest rect.
void DX11Renderer::Blit2DCenteredZoom(BlitObj2DIndexType iIndex, int iDestX, int iDestY, int iDestW, int iDestH, float zoomFactor)
{
    if (int(iIndex) < 0 || int(iIndex) >= MAX_TEXTURE_BUFFERS) return;

    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[int(iIndex)];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;

    // Clamp zoom factor to valid range (0.0–0.75)
    float z = std::clamp(zoomFactor, 0.0f, 0.75f);

    D2D1_SIZE_F sz = bitmap->GetSize();
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

    rt->DrawBitmap(bitmap.Get(), destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
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

    ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[int(iIndex)];
    ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
    if (!bitmap || !rt) return;

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
    rt->DrawBitmap(bitmap.Get(), dest1, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src1);

    // Part 2: Bottom-left (wrap X)
    if (destW1 < iTileSizeX)
    {
        int wrapW = iTileSizeX - destW1;
        D2D1_RECT_F src2 = D2D1::RectF(0, (float)iYOffset, (float)(bmpW - srcW1), (float)bmpH);
        D2D1_RECT_F dest2 = D2D1::RectF((float)(iBlitX + destW1), (float)iBlitY, (float)(iBlitX + iTileSizeX), (float)(iBlitY + destH1));
        rt->DrawBitmap(bitmap.Get(), dest2, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src2);
    }

    // Part 3: Top-right (wrap Y)
    if (destH1 < iTileSizeY)
    {
        int wrapH = iTileSizeY - destH1;
        D2D1_RECT_F src3 = D2D1::RectF((float)iXOffset, 0, (float)bmpW, (float)(bmpH - srcH1));
        D2D1_RECT_F dest3 = D2D1::RectF((float)iBlitX, (float)(iBlitY + destH1), (float)(iBlitX + destW1), (float)(iBlitY + iTileSizeY));
        rt->DrawBitmap(bitmap.Get(), dest3, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src3);
    }

    // Part 4: Top-left corner (wrap X and Y)
    if (destW1 < iTileSizeX && destH1 < iTileSizeY)
    {
        D2D1_RECT_F src4 = D2D1::RectF(0, 0, (float)(bmpW - srcW1), (float)(bmpH - srcH1));
        D2D1_RECT_F dest4 = D2D1::RectF((float)(iBlitX + destW1), (float)(iBlitY + destH1), (float)(iBlitX + iTileSizeX), (float)(iBlitY + iTileSizeY));
        rt->DrawBitmap(bitmap.Get(), dest4, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src4);
    }
}

void DX11Renderer::Cleanup() {
    // Prevent double-cleanup which can cause use-after-free on COM resources.
    if (bHasCleanedUp) {
        return;
    }

    // Mark engine shutdown intent as early as possible so threads can exit their loops.
    threadManager.threadVars.bIsShuttingDown.store(true);

    // Acquire an exclusive shutdown lock so no other code can manipulate DirectX resources during cleanup.
    ThreadLockHelper shutdownLock(threadManager, "exclusive_renderer_shutdown", 5000);
    if (!shutdownLock.IsLocked()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[RENDERER] Cleanup() - Failed to acquire exclusive shutdown lock");
        #endif
        return;
    }

    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] Cleanup() - Beginning safe shutdown sequence");
    #endif

    // Ensure the renderer thread is paused and no frame is currently in-flight.
    #if defined(RENDERER_IS_THREAD)
        try {
            WaitToFinishThenPauseThread();                              // Safely pause renderer thread and flush GPU work
        }
        catch (...) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] Cleanup() - Exception while pausing renderer thread");
            #endif
        }
    #endif

    // Request the loader thread to stop first, then join it via ThreadManager to avoid detach races.
    try {
        threadManager.StopThread(THREAD_LOADER);                        // Request loader stop
        threadManager.TerminateThread(THREAD_LOADER);                   // Join loader thread safely
    }
    catch (...) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] Cleanup() - Exception while terminating loader thread");
        #endif
    }

    // Request the renderer thread to stop and join it next.
    #if defined(RENDERER_IS_THREAD)
        try {
            threadManager.StopThread(THREAD_RENDERER);                  // Request render stop
            threadManager.TerminateThread(THREAD_RENDERER);             // Join render thread safely
        }
        catch (...) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] Cleanup() - Exception while terminating renderer thread");
            #endif
        }
    #endif

    // From this point forward, no engine threads should be touching DirectX objects.
    // Take the render mutex to protect against any stray access in non-threaded code paths.
    {
        std::lock_guard<std::mutex> renderGuard(s_renderMutex);

        // If we still have a context, unbind everything to release references to swap chain buffers.
        if (m_d3dContext) {
            ID3D11RenderTargetView* nullRTV[1] = { nullptr };           // Null RTV binding array
            ID3D11ShaderResourceView* nullSRV[16] = { nullptr };        // Null SRV array for unbinding
            ID3D11Buffer* nullVB[1] = { nullptr };                      // Null vertex buffer
            UINT nullStride = 0;                                        // Null stride
            UINT nullOffset = 0;                                        // Null offset

            // Unbind render targets.
            m_d3dContext->OMSetRenderTargets(1, nullRTV, nullptr);

            // Unbind shader resources to remove texture references.
            m_d3dContext->PSSetShaderResources(0, 16, nullSRV);
            m_d3dContext->VSSetShaderResources(0, 16, nullSRV);

            // Unbind input assembler buffers.
            m_d3dContext->IASetVertexBuffers(0, 1, nullVB, &nullStride, &nullOffset);
            m_d3dContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

            // Clear state and flush to ensure the driver releases internal references.
            m_d3dContext->ClearState();
            m_d3dContext->Flush();
        }

        // Release our 2D textures.
        for (int i = 0; i < MAX_TEXTURE_BUFFERS; i++) {
            m_d2dTextures[i].Reset();
            m_d2dTextures[i] = nullptr;
        }

        // Release our 3D textures.
        for (int i = 0; i < MAX_TEXTURE_BUFFERS_3D; i++) {
            m_d3dTextures[i].Reset();
            m_d3dTextures[i] = nullptr;
        }

        // Release Direct2D and related resources.
        m_pixelBrush.Reset();
        m_videoBitmap.Reset();
        m_videoStagingTex.Reset(); m_videoStagingW = 0; m_videoStagingH = 0;
        m_quadTexVB.Reset();
        m_quadRectVB.Reset();
        InvalidateTextFormatCache();
        m_d2dDevice.Reset();
        m_d2dContext.Reset();
        m_d2dRenderTarget.Reset();
        m_dwriteFactory.Reset();
        m_d2dFactory.Reset();

        // Release Direct3D render target and depth resources.
        m_renderTargetView.Reset();
        m_depthStencilView.Reset();
        m_depthStencilBuffer.Reset();

        // Release pipeline state objects and constant buffers.
        m_wireframeState.Reset();
        m_globalLightBuffer.Reset();
        m_cameraConstantBuffer.Reset();

        // Only ever included during development debugging.
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_RENDER_WIREFRAME_)
            m_wireframeState.Reset();
        #endif

        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
            m_debugBuffer.Reset();
        #endif

        // DXGI mandates SetFullscreenState(FALSE) before swap chain release when in exclusive fullscreen.
        // Skipping this leaves the display locked at the exclusive resolution until the next reboot.
        if (m_isExclusiveFullscreen && m_swapChain) {
            m_swapChain->SetFullscreenState(FALSE, nullptr);
            m_isExclusiveFullscreen = false;
            if (m_originalDesktopMode.dmSize > 0)
                ChangeDisplaySettingsEx(nullptr, &m_originalDesktopMode, nullptr, 0, nullptr);
        }

        // Release swap chain last, then device context and device.
        m_swapChain.Reset();
        m_d3dContext.Reset();
        m_d3dDevice.Reset();
    }

    // Restore OS cursor state.
    sysUtils.EnableMouseCursor();

    // Update base renderer state flags.
    bIsInitialized.store(false);
    bIsDestroyed.store(true);
    Renderer::bHasCleanedUp.store(true);

    // Update DX11Renderer local cleanup guard.
    bHasCleanedUp = true;

    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] Cleanup() - Completed safe shutdown sequence");
    #endif
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

    // GPU capability detection — same fields logged by DX12 for parity
    {
        DXGI_ADAPTER_DESC adDesc{};
        if (SUCCEEDED(dxgiAdapter->GetDesc(&adDesc)))
        {
            m_dedicatedVRAMMB   = static_cast<UINT64>(adDesc.DedicatedVideoMemory / (1024 * 1024));
            m_sharedSystemMemMB = static_cast<UINT64>(adDesc.SharedSystemMemory   / (1024 * 1024));
            m_isUMA             = (m_dedicatedVRAMMB == 0);
            m_isLowEndGPU       = (m_dedicatedVRAMMB < 2048 || m_isUMA);
            std::wstring gpuName(adDesc.Description, adDesc.Description + wcslen(adDesc.Description));
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"DX11Renderer: GPU Caps: %s, VRAM: %llu MB, Shared: %llu MB, UMA: %s, LowEnd: %s",
                gpuName.c_str(), m_dedicatedVRAMMB, m_sharedSystemMemMB,
                m_isUMA ? L"Yes" : L"No", m_isLowEndGPU ? L"Yes" : L"No");
            if (m_isLowEndGPU)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX11Renderer: Low-end GPU detected — reduced VRAM or integrated/UMA architecture.");
        }
    }

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
    swapDesc.BufferCount = config.myConfig.buffering ? 3 : 2;   // 3=triple / 2=double per config
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
    if (!m_dwriteFactory) { ThrowError("DirectWrite factory is not initialized."); return 0.0f; }

    IDWriteTextFormat* fmt = GetOrCreateTextFormat(FontName, FontSize);
    if (!fmt) { ThrowError("Failed to get text format for character width."); return 0.0f; }

    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(&character, 1, fmt, 1000.0f, 1000.0f, &textLayout);
    if (FAILED(hr)) { ThrowError("Failed to create text layout for character width."); return 0.0f; }

    DWRITE_TEXT_METRICS textMetrics;
    if (FAILED(textLayout->GetMetrics(&textMetrics))) { ThrowError("Failed to get text metrics."); return 0.0f; }
    return textMetrics.width;
}

float DX11Renderer::GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& fontName) {
    if (!m_dwriteFactory) { ThrowError("DirectWrite factory is not initialized."); return 0.0f; }

    IDWriteTextFormat* fmt = GetOrCreateTextFormat(fontName.c_str(), FontSize);
    if (!fmt) { ThrowError("Failed to get text format for character width with custom font."); return 0.0f; }

    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        &character, 1, fmt, 1000.0f, 1000.0f, &textLayout
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

float DX11Renderer::CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth)
{
    if (!m_dwriteFactory) { ThrowError("DirectWrite factory is not initialized."); return 0.0f; }

    IDWriteTextFormat* fmt = GetOrCreateTextFormat(FontName, FontSize);
    if (!fmt) { ThrowError("Failed to get text format for width calculation."); return 0.0f; }

    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.length()), fmt, containerWidth, 1000.0f, &textLayout);
    if (FAILED(hr)) { ThrowError("Failed to create text layout for width calculation."); return 0.0f; }

    DWRITE_TEXT_METRICS textMetrics;
    if (FAILED(textLayout->GetMetrics(&textMetrics))) { ThrowError("Failed to get text metrics."); return 0.0f; }

    float centerX = (containerWidth - textMetrics.width) / 2.0f;
    return (centerX < 0.0f) ? 0.0f : centerX;
}

float DX11Renderer::CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) {
    if (!m_dwriteFactory) { ThrowError("DirectWrite factory is not initialized."); return 0.0f; }

    IDWriteTextFormat* fmt = GetOrCreateTextFormat(FontName, FontSize);
    if (!fmt) { ThrowError("Failed to get text format for height calculation."); return 0.0f; }

    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.length()), fmt, 1000.0f, 1000.0f, &textLayout);
    if (FAILED(hr)) { ThrowError("Failed to create text layout for height calculation."); return 0.0f; }

    DWRITE_TEXT_METRICS textMetrics;
    if (FAILED(textLayout->GetMetrics(&textMetrics))) { ThrowError("Failed to get text metrics."); return 0.0f; }

    return textMetrics.height;
}

void DX11Renderer::DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) {
#ifdef RENDERER_IS_THREAD
    std::lock_guard<std::mutex> lock(s_renderMutex);
#endif
    if (is2D) {
        if (!m_d2dRenderTarget) return;
        DirectX::XMFLOAT4 c = ConvertColor(color.r, color.g, color.b, color.a);
        if (!m_pixelBrush)
            m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(c.x, c.y, c.z, c.w), &m_pixelBrush);
        else
            m_pixelBrush->SetColor(D2D1::ColorF(c.x, c.y, c.z, c.w));
        m_d2dRenderTarget->FillRectangle(
            D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y),
            m_pixelBrush.Get());
    }
    else {
        if (!m_quadRectVB) return;
        struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT4 color; };
        DirectX::XMFLOAT4 c = ConvertColor(color.r, color.g, color.b, color.a);
        Vertex vertices[] = {
            { { position.x,          position.y,          0.0f }, c },
            { { position.x + size.x, position.y,          0.0f }, c },
            { { position.x + size.x, position.y + size.y, 0.0f }, c },
            { { position.x,          position.y + size.y, 0.0f }, c }
        };

        D3D11_MAPPED_SUBRESOURCE ms = {};
        if (FAILED(m_d3dContext->Map(m_quadRectVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
        memcpy(ms.pData, vertices, sizeof(vertices));
        m_d3dContext->Unmap(m_quadRectVB.Get(), 0);

        UINT stride = sizeof(Vertex), offset = 0;
        m_d3dContext->IASetVertexBuffers(0, 1, m_quadRectVB.GetAddressOf(), &stride, &offset);
        m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_d3dContext->Draw(4, 0);
    }
}

void DX11Renderer::DrawCircle(const Vector2& center, float radius, const MyColor& color, bool filled) {
    if (!m_d2dRenderTarget) return;
    float fr = color.r / 255.0f, fg = color.g / 255.0f, fb = color.b / 255.0f, fa = color.a / 255.0f;
    ComPtr<ID2D1SolidColorBrush> brush;
    m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(fr, fg, fb, fa), &brush);
    if (!brush) return;
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radius, radius);
    if (filled) m_d2dRenderTarget->FillEllipse(ellipse, brush.Get());
    else        m_d2dRenderTarget->DrawEllipse(ellipse, brush.Get());
}

void DX11Renderer::PushClipRect(float x, float y, float w, float h) {
    if (!m_d2dRenderTarget) return;
    m_d2dRenderTarget->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h),
                                           D2D1_ANTIALIAS_MODE_ALIASED);
}

void DX11Renderer::PopClipRect() {
    if (!m_d2dRenderTarget) return;
    m_d2dRenderTarget->PopAxisAlignedClip();
}

void DX11Renderer::DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight, bool /*bold*/) {
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    // Bold weight matches the DX12/Vulkan pipeline treatment.
    // Format is cached per (font, size, weight) — only the layout (which captures the text
    // string) is created per call, which is unavoidable for metric measurement.
    IDWriteTextFormat* textFormat = GetOrCreateTextFormat(FontName, FontSize, DWRITE_FONT_WEIGHT_BOLD);
    if (!textFormat) return;

    // Measure actual text extents via TextLayout so the centred position is
    // exact at any DPI.  DWRITE_PARAGRAPH_ALIGNMENT_CENTER alone is unreliable
    // on ID2D1RenderTarget (DXGI surface) at non-96-DPI system scales.
    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()),
        textFormat, 10000.0f, 1000.0f, &textLayout);
    if (FAILED(hr)) return;

    DWRITE_TEXT_METRICS metrics;
    if (FAILED(textLayout->GetMetrics(&metrics))) return;

    // Compute the pixel-perfect centred origin within the control rect
    float centredX = position.x + (controlWidth  * 0.5f) - (metrics.width  * 0.5f);
    float centredY = position.y + (controlHeight * 0.5f) - (metrics.height * 0.5f);

    float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    if (!m_pixelBrush)
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &m_pixelBrush);
    else
        m_pixelBrush->SetColor(D2D1::ColorF(r, g, b, a));

    // Draw at the exact measured rect — horizontal and vertical centering is guaranteed
    m_d2dRenderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()), textFormat,
        D2D1::RectF(centredX, centredY, centredX + metrics.width, centredY + metrics.height),
        m_pixelBrush.Get()
    );
}

void DX11Renderer::DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) {
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    IDWriteTextFormat* textFormat = GetOrCreateTextFormat(FontName, FontSize);
    if (!textFormat) return;

    float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    if (!m_pixelBrush)
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &m_pixelBrush);
    else
        m_pixelBrush->SetColor(D2D1::ColorF(r, g, b, a));

    m_d2dRenderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()), textFormat,
        D2D1::RectF(position.x, position.y, position.x + 1000.0f, position.y + 200.0f),
        m_pixelBrush.Get()
    );
}

void DX11Renderer::DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) {
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    IDWriteTextFormat* fmt = GetOrCreateTextFormat(FontName, FontSize);
    if (!fmt) return;

    float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    if (!m_pixelBrush)
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &m_pixelBrush);
    else
        m_pixelBrush->SetColor(D2D1::ColorF(r, g, b, a));

    m_d2dRenderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()), fmt,
        D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y),
        m_pixelBrush.Get()
    );
}

void DX11Renderer::DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color,
    const float FontSize, const std::wstring& fontName) {
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || FontSize <= 0.0f) return;

    IDWriteTextFormat* textFormat = GetOrCreateTextFormat(fontName.c_str(), FontSize);
    if (!textFormat) return;

    float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    if (!m_pixelBrush)
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &m_pixelBrush);
    else
        m_pixelBrush->SetColor(D2D1::ColorF(r, g, b, a));

    m_d2dRenderTarget->DrawText(
        text.c_str(), static_cast<UINT32>(text.size()), textFormat,
        D2D1::RectF(position.x, position.y, position.x + 1000.0f, position.y + 200.0f),
        m_pixelBrush.Get()
    );
}

void DX11Renderer::DrawMyTextStyled(const std::wstring& text, const Vector2& position,
    const MyColor& color, const TextRenderStyle& style)
{
    if (!m_d2dRenderTarget || !m_dwriteFactory) return;
    if (text.empty() || style.fontSize <= 0.0f) return;

    // Create a text format with the requested weight and style.
    // Not cached because weight/style vary per call.
    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        style.fontName.empty() ? L"Arial" : style.fontName.c_str(),
        nullptr,
        style.bold   ? DWRITE_FONT_WEIGHT_BOLD   : DWRITE_FONT_WEIGHT_NORMAL,
        style.italic ? DWRITE_FONT_STYLE_ITALIC   : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        style.fontSize,
        L"en-us",
        &fmt);
    if (FAILED(hr) || !fmt) return;

    // Create a text layout so we can apply underline / strikethrough per-range.
    UINT32 textLen = static_cast<UINT32>(text.size());

    float layoutWidth = 2000.0f;
    float drawX = position.x;
    if (style.centered) {
        float rtWidth = (m_renderTargetWidth > 0)
                        ? static_cast<float>(m_renderTargetWidth)
                        : 1920.0f;
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layoutWidth = rtWidth;
        drawX = 0.0f;
    }

    ComPtr<IDWriteTextLayout> layout;
    hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), textLen, fmt.Get(), layoutWidth, 500.0f, &layout);
    if (FAILED(hr) || !layout) return;

    DWRITE_TEXT_RANGE all{ 0, textLen };
    if (style.underline)      layout->SetUnderline(TRUE,  all);
    if (style.strikethrough)  layout->SetStrikethrough(TRUE, all);

    float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    if (!m_pixelBrush)
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &m_pixelBrush);
    else
        m_pixelBrush->SetColor(D2D1::ColorF(r, g, b, a));

    m_d2dRenderTarget->DrawTextLayout(
        D2D1::Point2F(drawX, position.y),
        layout.Get(),
        m_pixelBrush.Get());
}

void DX11Renderer::DrawTexture(int textureIndex, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) {
#ifdef RENDERER_IS_THREAD
    std::lock_guard<std::mutex> lock(s_renderMutex);
#endif

    if (is2D) {
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS) return;
        ComPtr<ID2D1Bitmap>       bitmap = m_d2dTextures[textureIndex];
        ComPtr<ID2D1RenderTarget> rt     = m_d2dRenderTarget;
        if (!bitmap || !rt) return;

        rt->DrawBitmap(
            bitmap.Get(),
            D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y),
            tintColor.a,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            nullptr
        );
    }
    else {
        if (textureIndex < 0 || textureIndex >= MAX_TEXTURE_BUFFERS_3D || !m_d3dTextures[textureIndex]) return;
        if (!m_quadTexVB) return;

        struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; };
        Vertex vertices[] = {
            { { position.x,          position.y,          0.0f }, { 0.0f, 0.0f } },
            { { position.x + size.x, position.y,          0.0f }, { 1.0f, 0.0f } },
            { { position.x + size.x, position.y + size.y, 0.0f }, { 1.0f, 1.0f } },
            { { position.x,          position.y + size.y, 0.0f }, { 0.0f, 1.0f } }
        };

        D3D11_MAPPED_SUBRESOURCE ms = {};
        if (FAILED(m_d3dContext->Map(m_quadTexVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
        memcpy(ms.pData, vertices, sizeof(vertices));
        m_d3dContext->Unmap(m_quadTexVB.Get(), 0);

        m_d3dContext->PSSetShaderResources(0, 1, m_d3dTextures[textureIndex].GetAddressOf());
        UINT stride = sizeof(Vertex), offset = 0;
        m_d3dContext->IASetVertexBuffers(0, 1, m_quadTexVB.GetAddressOf(), &stride, &offset);
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
        D3D11_TEXTURE2D_DESC textureDesc = {};
        videoTexture->GetDesc(&textureDesc);

        // Reset the D2D bitmap only when video dimensions change
        if (m_videoStagingW != textureDesc.Width || m_videoStagingH != textureDesc.Height)
        {
            m_videoBitmap.Reset();
            m_videoStagingW = textureDesc.Width;
            m_videoStagingH = textureDesc.Height;
        }

        // The video texture is STAGING (CPU_ACCESS_READ) — map directly.
        // Previously used CopyResource(STAGING, DYNAMIC) which the D3D11 spec forbids
        // (CopyResource source must not be DYNAMIC); that triggered an SDK Layer exception.
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_d3dContext->Map(videoTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to map video texture"); return; }

        ThreadLockHelper d2dLock(threadManager, "d2d_draw_lock", 1000);
        if (!d2dLock.IsLocked()) {
            m_d3dContext->Unmap(videoTexture.Get(), 0);
            return;
        }

        // Reuse the D2D bitmap across frames — update pixels via CopyFromMemory instead of recreating
        if (!m_videoBitmap)
        {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            hr = m_d2dRenderTarget->CreateBitmap(
                D2D1::SizeU(textureDesc.Width, textureDesc.Height),
                mapped.pData, mapped.RowPitch, props, &m_videoBitmap);
        }
        else
        {
            D2D1_RECT_U updateRect = D2D1::RectU(0, 0, textureDesc.Width, textureDesc.Height);
            hr = m_videoBitmap->CopyFromMemory(&updateRect, mapped.pData, mapped.RowPitch);
        }

        m_d3dContext->Unmap(videoTexture.Get(), 0);

        if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to update video bitmap"); return; }

        m_d2dRenderTarget->DrawBitmap(
            m_videoBitmap.Get(),
            D2D1::RectF(position.x, position.y, position.x + size.x, position.y + size.y),
            tintColor.a,
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
bool DX11Renderer::Resize(uint32_t width, uint32_t height)
{
    // Acquire comprehensive resize lock to prevent conflicts with other subsystems
    ThreadLockHelper comprehensiveResizeLock(threadManager, "comprehensive_resize_lock", 5000);
    if (!comprehensiveResizeLock.IsLocked()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RESIZE] Could not acquire comprehensive resize lock - aborting resize operation");
        #endif
        return false;
    }

    // Acquire exclusive DirectX access lock to prevent concurrent DirectX usage (render thread, loader thread, etc.)
    ThreadLockHelper exclusiveDirectXLock(threadManager, "exclusive_directx_access", 10000);
    if (!exclusiveDirectXLock.IsLocked()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RESIZE] Could not acquire exclusive DirectX lock - aborting resize operation");
        #endif
        return false;
    }

    // Serialize renderer-side DirectX entry points that use s_renderMutex (RenderFrame, loader, etc.)
    std::lock_guard<std::mutex> renderGuard(s_renderMutex);

    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[RESIZE] Beginning resize operation to %dx%d", width, height);
    #endif

    // Validate critical DirectX interfaces
    if (!m_swapChain || !m_d3dDevice || !m_d3dContext) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[RESIZE] Missing critical DirectX interfaces - cannot resize");
        #endif
        return false;
    }

    // Save current dimensions for comparison and rollback
    UINT oldWidth = iOrigWidth;
    UINT oldHeight = iOrigHeight;

    // Validate resize parameters
    if (width < 1 || height < 1) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RESIZE] Invalid dimensions - skipping resize");
        #endif
        return false;
    }

    // Check if resize is actually needed
    if (width == oldWidth && height == oldHeight) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Dimensions unchanged - skipping resize operation");
        #endif
        return false;
    }

    try {
        // Ensure all GPU operations are complete before tearing down resources
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 1: Flushing GPU and waiting for completion");
        #endif

        m_d3dContext->Flush();
        WaitForGPUToFinish();

        // Release Direct2D references that might hold swap chain buffers.
        // Order matters: release all resources derived FROM the device first,
        // then the render target, then the context and device itself.
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 2: Releasing Direct2D references");
        #endif

        if (m_d2dContext) {
            m_d2dContext->SetTarget(nullptr);
            m_d2dContext->Flush();
        }

        // Step 3 (moved up): release D2D bitmaps and brush BEFORE the render target
        // so that no derived resource holds the render target's ref count above zero.
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 3: Cleaning 2D textures and brushes");
        #endif
        Clean2DTextures();
        m_pixelBrush.Reset();
        m_videoBitmap.Reset();  // tied to m_d2dRenderTarget — must release before it

        // Now release the render target (no derived resources alive → ref count reaches 0
        // immediately, releasing the internal IDXGISurface ref on the swap chain back buffer).
        m_d2dRenderTarget.Reset();

        // Release the device context and D2D device last; the device may hold internal
        // GPU caches tied to the DXGI device — flushing it here releases those.
        m_d2dContext.Reset();
        m_d2dDevice.Reset();
        dxgiSurface.Reset();

        // Flush GPU again now that all D2D objects (which wrap the DXGI surface) are released,
        // ensuring the driver processes the releases before we unbind the D3D pipeline.
        m_d3dContext->Flush();
        WaitForGPUToFinish();

        // Unbind everything from the pipeline so no outstanding references remain
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 4: Clearing D3D pipeline bindings");
        #endif

        // Unbind all render targets
        m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);

        // Clear SRVs across all shader stages and all slots
        ID3D11ShaderResourceView* nullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
        m_d3dContext->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
        m_d3dContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
        m_d3dContext->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
        m_d3dContext->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
        m_d3dContext->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
        m_d3dContext->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);

        // Clear vertex and index buffers
        ID3D11Buffer* nullVB[1] = { nullptr };
        UINT nullStride = 0;
        UINT nullOffset = 0;
        m_d3dContext->IASetVertexBuffers(0, 1, nullVB, &nullStride, &nullOffset);
        m_d3dContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

        // Clear full state to release internal references
        m_d3dContext->ClearState();
        m_d3dContext->Flush();
        WaitForGPUToFinish();

        // Release RTV/DSV resources
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 5: Releasing RTV/DSV resources");
        #endif

        m_renderTargetView.Reset();
        m_depthStencilView.Reset();
        m_depthStencilBuffer.Reset();

        // Resize swap chain buffers
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 6: Resizing swap chain buffers to %dx%d", width, height);
        #endif

        HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[RESIZE] ResizeBuffers failed with HRESULT: 0x%08X", hr);
            #endif
            throw std::runtime_error("DirectX ResizeBuffers operation failed");
        }

        // Recreate RTV
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 7: Recreating render target view");
        #endif

        ComPtr<ID3D11Texture2D> backBuffer;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[RESIZE] Failed to get new back buffer: 0x%08X", hr);
            #endif
            throw std::runtime_error("Failed to retrieve new back buffer");
        }

        hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
        if (FAILED(hr)) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[RESIZE] Failed to create new render target view: 0x%08X", hr);
            #endif
            throw std::runtime_error("Failed to create new render target view");
        }

        // Update tracked render target settings from new back buffer
        D3D11_TEXTURE2D_DESC backDesc = {};
        backBuffer->GetDesc(&backDesc);
        m_renderTargetWidth = backDesc.Width;
        m_renderTargetHeight = backDesc.Height;
        m_renderTargetSampleCount = backDesc.SampleDesc.Count;
        m_renderTargetSampleQuality = backDesc.SampleDesc.Quality;

        // Recreate depth stencil buffer matching back buffer
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 8: Recreating depth stencil buffer");
        #endif

        CreateDepthStencilBuffer();

        // Update viewport
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 9: Updating viewport");
        #endif

        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(m_renderTargetWidth);
        vp.Height = static_cast<float>(m_renderTargetHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        m_d3dContext->RSSetViewports(1, &vp);

        // Bind new targets
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 10: Binding new render targets");
        #endif

        m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

        // Restore pipeline states cleared by ClearState() so models render again
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 11: Restoring pipeline states");
        #endif

        SetupPipelineStates();

        // Recreate Direct2D resources after swap chain is stable
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RESIZE] Step 12: Recreating Direct2D resources");
        #endif

        CreateDirect2DResources();

        // Update internal dimension tracking
        iOrigWidth = width;
        iOrigHeight = height;

        // Update camera and config with the correct canonical aspect ratio
        {
            float newAR = LookupAspectRatio(static_cast<int>(width), static_cast<int>(height));
            config.myConfig.aspectRatio = newAR;
            myCamera.UpdateResolution(width, height, newAR);
        }

        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[RESIZE] Resize completed successfully - Old: %dx%d, New: %dx%d",
                oldWidth, oldHeight, iOrigWidth, iOrigHeight);
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[RESIZE] Exception occurred during resize operation: %hs", e.what());
        #endif

        // Roll back internal dimensions on failure
        iOrigWidth = oldWidth;
        iOrigHeight = oldHeight;

        throw;
        return false;
    }

    return true;
}


// *-------------------------------------------------------------------------------------*
// WaitToFinishThenPauseThread - Safely waits for renderer to complete current operations
// then pauses the renderer thread to allow for safe resource cleanup during resize
// *-------------------------------------------------------------------------------------*
void DX11Renderer::WaitToFinishThenPauseThread() {
    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Beginning enhanced safe thread pause sequence");
    #endif

    // Step 1: Acquire exclusive DirectX access lock to prevent concurrent operations
    ThreadLockHelper exclusiveDirectXLock(threadManager, "exclusive_directx_access", 10000);
    if (!exclusiveDirectXLock.IsLocked()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] WaitToFinishThenPauseThread() - Failed to acquire exclusive DirectX lock");
        #endif
        return;
    }

    // Step 2: Wait for current rendering operations to complete with enhanced monitoring
    int waitAttempts = 0;                                               // Counter to prevent infinite waiting
    const int maxWaitAttempts = 500;                                    // Maximum wait cycles (5 seconds at 10ms intervals)
    
    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Waiting for render operations to complete");
    #endif
    
    while (threadManager.threadVars.bIsRendering.load() && waitAttempts < maxWaitAttempts) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            if (waitAttempts % 100 == 0 && waitAttempts > 0) {          // Log every 1000ms to avoid spam
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[RENDERER] WaitToFinishThenPauseThread() - Still waiting for render completion, attempt %d", waitAttempts);
            }
        #endif
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));     // Small delay to prevent CPU spinning
        waitAttempts++;                                                 // Increment wait counter
    }

    // Step 3: Check if we timed out waiting for renderer
    if (waitAttempts >= maxWaitAttempts) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] WaitToFinishThenPauseThread() - Timeout waiting for renderer to finish, forcing pause");
        #endif
    } else {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Renderer completed after %d wait cycles", waitAttempts);
        #endif
    }

    // Step 4: Additional DirectX-specific synchronization
    if (m_d3dContext) {
        try {
            // Flush all pending DirectX commands to ensure completion
            m_d3dContext->Flush();                                       // Force completion of all queued commands
            
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - DirectX context flushed");
            #endif
        }
        catch (const std::exception& e) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERER] WaitToFinishThenPauseThread() - Exception during context flush: %hs", e.what());
            #endif
        }
    }

    // Step 5: Ensure GPU operations are complete with timeout
    try {
        WaitForGPUToFinish();                                           // Use existing GPU synchronization method
        
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - GPU operations completed");
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERER] WaitToFinishThenPauseThread() - Exception during GPU wait: %hs", e.what());
        #endif
    }

    // Step 6: Pause the renderer thread safely
    ThreadStatus rendererStatus = threadManager.GetThreadStatus(THREAD_RENDERER);
    if (rendererStatus == ThreadStatus::Running) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Pausing renderer thread");
        #endif
        
        threadManager.PauseThread(THREAD_RENDERER);                     // Pause the renderer thread
        
        // Step 7: Verify thread was successfully paused with extended timeout
        int pauseVerifyAttempts = 0;                                    // Counter for pause verification
        const int maxPauseVerifyAttempts = 200;                         // Maximum attempts to verify pause (2 seconds)
        
        while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running && 
               pauseVerifyAttempts < maxPauseVerifyAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Brief wait for thread state change
            pauseVerifyAttempts++;                                      // Increment verification counter
        }
        
        if (pauseVerifyAttempts >= maxPauseVerifyAttempts) {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERER] WaitToFinishThenPauseThread() - Thread pause verification timeout");
            #endif
        } else {
            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Thread successfully paused after %d verification cycles", pauseVerifyAttempts);
            #endif
        }
    } else {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Thread already in state: %d", static_cast<int>(rendererStatus));
        #endif
    }

    // Step 8: Final verification that no DirectX operations are active
    int finalVerifyAttempts = 0;                                        // Counter for final verification
    const int maxFinalVerifyAttempts = 50;                              // Maximum attempts for final verification
    
    while (threadManager.threadVars.bIsRendering.load() && finalVerifyAttempts < maxFinalVerifyAttempts) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));     // Brief wait for operations to cease
        finalVerifyAttempts++;                                          // Increment final verification counter
    }

    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        if (finalVerifyAttempts > 0) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Final verification completed after %d cycles", finalVerifyAttempts);
        }
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERER] WaitToFinishThenPauseThread() - Enhanced safe thread pause sequence completed successfully");
    #endif

    // Note: exclusiveDirectXLock will be automatically released when it goes out of scope
}

void DX11Renderer::ResumeLoader(bool isResizing)
{
    // Track whether this resume is for a resize-only reload (textures only) or a full scene reload.
    if (isResizing) {
        wasResizing.store(true);
    } else {
        wasResizing.store(false);
    }

    // Ensure we clear any transient Direct2D busy state so the loader can proceed.
    D2DBusy.store(false);

    // Mark loader as not finished so RenderFrame can throttle rendering while assets are loading.
    threadManager.threadVars.bLoaderTaskFinished.store(false);

    // Acquire a loader control lock to prevent races with shutdown and other loader control paths.
    ThreadLockHelper loaderControlLock(threadManager, "loader_control_operation", 5000);
    if (!loaderControlLock.IsLocked()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER] ResumeLoader() - Failed to acquire loader control lock");
        #endif
        return;
    }

    // Query current loader thread state.
    ThreadStatus tstat = threadManager.GetThreadStatus(THREAD_LOADER);

    try
    {
        // If the loader thread exists and is paused, resume it.
        if (tstat == ThreadStatus::Running || tstat == ThreadStatus::Paused)
        {
            threadManager.ResumeThread(THREAD_LOADER);

            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER] ResumeLoader() - Loader thread resumed");
            #endif

            return;
        }

        // If the loader thread does not exist, create it again through ThreadManager and start it.
        if (tstat == ThreadStatus::Stopped || tstat == ThreadStatus::Terminated || !threadManager.DoesThreadExist(THREAD_LOADER))
        {
            threadManager.SetThread(THREAD_LOADER, [this]() { this->LoaderTaskThread(); }, true);
            threadManager.StartThread(THREAD_LOADER);

            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER] ResumeLoader() - Loader thread restarted");
            #endif

            return;
        }
    }
    catch (const std::exception& e)
    {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[LOADER] ResumeLoader() - Exception: %hs", e.what());
        #endif
    }
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
    viewport.Width    = static_cast<float>(m_renderTargetWidth);
    viewport.Height   = static_cast<float>(m_renderTargetHeight);
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
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX11Renderer] LoadShaders() called - using ShaderManager for shader loading.");
    #endif

    // Use the external shader manager to load and create required shader programs.
    // The shaders are loaded in Main.cpp via LoadAllShaders() call in ShaderLoaders.cpp file.
    // Here we just verify that the required programs exist and are ready to use.

    // ------------------------------------------------------------
    // Model rendering requires ModelProgram (preferred) or GameplayModelProgram (fallback).
    // ------------------------------------------------------------
    const bool hasModelProgram = shaderManager.DoesProgramExist("ModelProgram");
    const bool hasGameplayModelProgram = shaderManager.DoesProgramExist("GameplayModelProgram");

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[DX11Renderer] Shader Programs: ModelProgram=%s, GameplayModelProgram=%s",
            hasModelProgram ? L"True" : L"False",
            hasGameplayModelProgram ? L"True" : L"False");
    #endif

    if (!hasModelProgram && !hasGameplayModelProgram)
    {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX11Renderer] LoadShaders() failed - neither ModelProgram nor GameplayModelProgram exists in ShaderManager.");
        #endif

        // Uh Oh! - You have done something wrong here!
        ThrowError("Required shader program 'ModelProgram' (or fallback 'GameplayModelProgram') not available from ShaderManager");
        return;
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX11Renderer] LoadShaders() completed successfully - required model shader program available.");
    #endif
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

        // Clear D3D11 pipeline bindings so the context releases its RTV/DSV references
        // before we reset the views — without this, ResizeBuffers sees outstanding refs.
        if (m_d3dContext) {
            m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* nullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
            m_d3dContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
            m_d3dContext->ClearState();
            m_d3dContext->Flush();
        }
        WaitForGPUToFinish();

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
        iOrigWidth           = fullscreenWidth;                                 // Update width
        iOrigHeight          = fullscreenHeight;                                // Update height
        m_renderTargetWidth  = fullscreenWidth;                                 // Keep render target in sync
        m_renderTargetHeight = fullscreenHeight;

        // Update window metrics so mouse clamp and GUI layout use the real display size
        winMetrics.width        = fullscreenWidth;
        winMetrics.height       = fullscreenHeight;
        winMetrics.clientWidth  = fullscreenWidth;
        winMetrics.clientHeight = fullscreenHeight;

        // Update camera so 3D projection and screen-space calculations use real resolution
        {
            float newAR = LookupAspectRatio(static_cast<int>(fullscreenWidth), static_cast<int>(fullscreenHeight));
            myCamera.UpdateResolution(fullscreenWidth, fullscreenHeight, newAR);
        }

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

        // Capture the current desktop display mode before any mode change so it can be restored on exit
        m_originalDesktopMode = {};
        m_originalDesktopMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &m_originalDesktopMode);

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
        targetMode.RefreshRate.Numerator = static_cast<UINT>(config.myConfig.refreshRate);
        targetMode.RefreshRate.Denominator = 1;

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

        // Clear D3D11 pipeline bindings so the context releases its RTV/DSV references
        // before we reset the views — without this, ResizeBuffers sees outstanding refs.
        if (m_d3dContext) {
            m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* nullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
            m_d3dContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
            m_d3dContext->ClearState();
            m_d3dContext->Flush();
        }
        WaitForGPUToFinish();

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

        m_isExclusiveFullscreen = true;

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
        HRESULT hr;
        if (m_swapChain)
        {
            hr = m_swapChain->SetFullscreenState(FALSE, nullptr);
            if (FAILED(hr)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERER] Failed to set windowed state");
                bFullScreenTransition.store(false);
                threadManager.threadVars.bSettingFullScreen.store(false);
                return false;
            }
            m_isExclusiveFullscreen = false;
        }

        // If we are shutting down, we do not need to worry about resizing buffers
        if (threadManager.threadVars.bIsShuttingDown.load()) {
            bFullScreenTransition.store(false);
            threadManager.threadVars.bSettingFullScreen.store(false);
            return true;
        }

        // Get appropriate window dimensions from stored values
        UINT windowedWidth = prevWindowedWidth > 0 ? prevWindowedWidth : static_cast<UINT>(config.myConfig.resolutionWidth);
        UINT windowedHeight = prevWindowedHeight > 0 ? prevWindowedHeight : static_cast<UINT>(config.myConfig.resolutionHeight);

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

        // Clear D3D11 pipeline bindings so the context releases its RTV/DSV references
        // before we reset the views — without this, ResizeBuffers sees outstanding refs.
        if (m_d3dContext) {
            m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* nullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
            m_d3dContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
            m_d3dContext->ClearState();
            m_d3dContext->Flush();
        }
        WaitForGPUToFinish();

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
//        if (controlsWindow) score += 10000;  // Huge priority for adapters controlling the window
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

#pragma warning(pop)

#endif