/* -------------------------------------------------------------- */
// Main Tasking Thread for our DX12 renderer
//
// This function is the main (optional thread) function that will
// be used to render the scene. It will be responsible for rendering
// the scene, updating the scene, and handling any other rendering
// tasks.
//
// Pipeline order (DX12):
// 1) Initialize render, safety guards, acquire exclusive lock
// 2) Reset command allocator + list, set root signature & heaps
// 3) TransitionResource PRESENT → RENDER_TARGET, clear RT + DSV
// 4) Calculate delta time, update camera
// 5) RenderBackgroundImage() - D2D background before any 3D (scene-guarded)
// 6) OMSetRenderTargets (DX12), 3D models, animations, lighting (RenderGamePlay)
// 7) TransitionResource RENDER_TARGET → PRESENT, close + execute command list
// 8) FX rendering + D2D effects via fxManager (D2D context)
// 9) Text, GUI, FPS, cursor, REC indicator (D2D context)
// 10) Present frame via PresentFrame() + MoveToNextFrame()

/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ---------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_DIRECTX_12__)
#include "DX12Renderer.h"
#include "DX12RenderFrame.h"
#include "BuildInfo.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "DX12FXManager.h"
#include "GUIManager.h"
#include "ConsoleWindow.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"

extern HWND hwnd;
extern HINSTANCE hInst;
extern GUIManager guiManager;
extern ConsoleWindow consoleWindow;
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
extern MoviePlayer moviePlayer;
extern WindowMetrics winMetrics;
extern ScreenRecorder screenRecorder;
extern bool bResizing;
extern std::atomic<bool> bResizeInProgress;
extern std::atomic<bool> bFullScreenTransition;

#ifdef __USE_SCRIPT_MANAGER__
#include "ScriptManager.h"
extern ScriptManager scriptManager;
#endif

#pragma warning(push)
#pragma warning(disable: 4101)

// ===========================================================================================
// RenderFrame — main per-frame entry point for the DX12 rendering pipeline.
// ===========================================================================================
void DX12Renderer::RenderFrame()
{
    // ENHANCED SAFE-GUARDS — Check all critical conditions before proceeding
    if (bHasCleanedUp || !m_d3d12Device || !m_commandQueue || !m_constantBuffer)
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Early exit - missing critical resources");
        #endif
        return;
    }

    // Check for shutdown, minimised, resizing, or uninitialised states
    if (threadManager.threadVars.bIsShuttingDown.load() || bIsMinimized.load() ||
        threadManager.threadVars.bIsResizing.load()     || !bIsInitialized.load())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Early exit - system state prevents rendering");
        #endif
        return;
    }

    // CRITICAL: Prevent multiple render operations; acquire exclusive DX12 access
    ThreadLockHelper exclusiveRenderLock(threadManager, "exclusive_render_operation", 50);
    if (!exclusiveRenderLock.IsLocked())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Could not acquire exclusive render lock - skipping frame");
        #endif
        return;
    }

    // Double-check rendering state after acquiring lock
    if (threadManager.threadVars.bIsRendering.load())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RENDERFRAME] Another render operation already active - aborting");
        #endif
        return;
    }

    try
    {
        exceptionHandler.RecordFunctionCall("DX12Renderer::RenderFrame");

        // Set rendering state atomically to indicate we are now rendering
        threadManager.threadVars.bIsRendering.store(true);

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Beginning render operation");
        #endif

        HWND hWnd = hwnd;

        // Clear colour: very dark grey in debug (aids model visibility), pure black in release
        #if defined(_DEBUG)
            const float clearColor[4] = { 0.01f, 0.01f, 0.01f, 1.0f };
        #else
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        #endif

        D3D12_VIEWPORT viewport  = {};
        D3D12_RECT     scissorRect = {};
        RECT           rc;

        #ifdef RENDERER_IS_THREAD
            ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
            while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
                   (!threadManager.threadVars.bIsShuttingDown.load()))
            {
                status = threadManager.GetThreadStatus(THREAD_RENDERER);
                if (status == ThreadStatus::Paused)
                {
                    threadManager.threadVars.bIsRendering.store(false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                if (threadManager.threadVars.bIsResizing.load() || bIsMinimized.load())
                {
                    threadManager.threadVars.bIsRendering.store(false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                threadManager.threadVars.bIsRendering.store(true);
        #endif

            // CRITICAL RESOURCE CHECK — recover from lost swap chain or command queue
            if (!m_swapChain || !m_commandQueue || !m_fence)
            {
                if (!threadManager.threadVars.bIsResizing.load() && !sysUtils.IsWindowMinimized())
                {
                    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RENDERFRAME] Critical resources invalid. Attempting recovery.");
                    #endif
                    threadManager.threadVars.bIsResizing.store(true);
                    try {
                        Resize(iOrigWidth, iOrigHeight);
                        ResumeLoader();
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Recovery failed: %hs", e.what());
                        #endif
                    }
                    threadManager.threadVars.bIsResizing.store(false);
                    threadManager.threadVars.bIsRendering.store(false);
                    return;
                }
                else
                {
                    threadManager.threadVars.bIsRendering.store(false);
                    return;
                }
            }

            // STEP 1: Calculate viewport from fullscreen / windowed mode
            if (!winMetrics.isFullScreen)
                GetClientRect(hWnd, &rc);
            else
                rc = winMetrics.monitorFullArea;

            float width  = float(rc.right  - rc.left);
            float height = float(rc.bottom - rc.top);

            viewport.Width    = width;
            viewport.Height   = height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;

            scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

            // STEP 2: Wait for the previous frame, then reset the command list
            WaitForPreviousFrame();
            ResetCommandList();

            // STEP 3: Camera update + delta time
            myCamera.UpdateViewMatrix();
            myCamera.UpdateJumpAnimation();

            auto now         = std::chrono::steady_clock::now();
            float rawDelta   = std::chrono::duration<float>(now - lastFrameTime).count();
            float deltaTime  = deltaTimeSmoothing.ProcessDelta(rawDelta, 60.0f);
            deltaTime        = std::clamp(deltaTime, 0.001f, 0.1f);
            lastFrameTime    = now;

            #ifdef __USE_SCRIPT_MANAGER__
                scriptManager.Update(deltaTime);
            #endif

            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                static int frameCounter = 0;
                frameCounter++;
                if (frameCounter >= 60)
                {
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Delta time - Raw: %.6f, Smoothed: %.6f", rawDelta, deltaTime);
                    frameCounter = 0;
                }
            #endif

            // STEP 4: Set root signature, descriptor heaps, viewport, scissor
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
            ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
            m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
            m_commandList->RSSetViewports(1, &viewport);
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // STEP 5: Transition back buffer PRESENT → RENDER_TARGET
            TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_frameContexts[m_frameIndex].rtvHandle);
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);

            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

            // STEP 6: Clear render targets (no lock needed — render thread is the sole user)
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

            // STEP 7: Update + bind constant buffers via root descriptors
            UpdateConstantBuffers();

            if (m_constantBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER,
                    m_constantBuffer->GetGPUVirtualAddress());

            if (m_globalLightBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER,
                    m_globalLightBuffer->GetGPUVirtualAddress());

            if (m_envBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_ENVIRONMENT_BUFFER,
                    m_envBuffer->GetGPUVirtualAddress());

            // Set primitive topology
            m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // STEP 8: Scene-specific 3D rendering via DX12 command list
            switch (scene.stSceneType)
            {
                #if defined(_DEBUG)
                    case SceneType::SCENE_EXPERIMENT:
                    {
                        break;
                    }
                #endif

                case SceneType::SCENE_INTRO:
                case SceneType::SCENE_INTRO_MOVIE:
                    break;

                case SceneType::SCENE_GAMETITLE:
                {
                    if (threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        int iModelID = scene.FindParentModelID(SplashShipName);
                        if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                            scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    }
                    RenderGamePlay(deltaTime);
                    break;
                }

                case SceneType::SCENE_GAMEPLAY:
                {
                    int iModelID = scene.FindParentModelID(ShipName1);
                    if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                        scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    RenderGamePlay(deltaTime);
                    break;
                }

                default:
                    break;
            }

            // STEP 9: Close and execute 3D command list.
            // Deliberately leave the back buffer in RENDER_TARGET state so that
            // AcquireWrappedResources (inState=RENDER_TARGET) can hand it to D2D.
            CloseCommandList();
            ExecuteCommandList();

            // STEP 10: 2D rendering via DX11On12 / D2D interop.
            //
            // Back buffer is still in RENDER_TARGET state (left there by step 9).
            // AcquireWrappedResources hands it to D2D. ReleaseWrappedResources
            // transitions it to PRESENT so PresentFrame() can display it.
            if (m_d2dContext && m_dx11Dx12Compat.dx11On12Device &&
                m_wrappedBackBuffers[m_frameIndex] && m_d2dRenderTargets[m_frameIndex])
            {
                ID3D11Resource* wrappedRes = m_wrappedBackBuffers[m_frameIndex].Get();
                m_dx11Dx12Compat.dx11On12Device->AcquireWrappedResources(&wrappedRes, 1);
                m_d2dContext->SetTarget(m_d2dRenderTargets[m_frameIndex].Get());

                // ── Single D2D pass: background, 3D-behind FX, overlays ──────────
                // One BeginDraw/EndDraw per frame eliminates the inter-pass flush
                // overhead of the previous two-pass design.
                if ((!threadManager.threadVars.bIsShuttingDown.load()) &&
                    (!bIsMinimized.load()) && (!threadManager.threadVars.bIsResizing.load()) &&
                    bIsInitialized.load())
                {
                    m_d2dContext->BeginDraw();
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMETITLE:
                        {
                            if (threadManager.threadVars.bLoaderTaskFinished.load())
                            {
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)])
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);

                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
                                {
                                    D2D1_SIZE_F sz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO,
                                        0, iOrigHeight - static_cast<int>(sz.height * 0.5f),
                                        static_cast<int>(sz.width * 0.5f), static_cast<int>(sz.height * 0.5f));
                                }
                            }
                            else
                            {
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                                {
                                    if (threadManager.threadVars.bInitiateFader.load())
                                    {
                                        threadManager.threadVars.bInitiateFader.store(false);
                                        fxManager.FadeToImage(1.0f, 0.1f);
                                    }
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                                }
                            }
                            break;
                        }
                        case SceneType::SCENE_GAMEPLAY:
                        {
                            if (!threadManager.threadVars.bLoaderTaskFinished.load() &&
                                m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                            {
                                if (threadManager.threadVars.bInitiateFader.load())
                                {
                                    threadManager.threadVars.bInitiateFader.store(false);
                                    fxManager.FadeToImage(1.0f, 0.1f);
                                }
                                Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                            }
                            break;
                        }
                        default:
                            break;
                    }

                    // Background FX (starfield, warp tunnel) behind 3D content.
                    try { fxManager.Render(true); }
                    catch (const std::exception&) {}

                    // ── Scene-specific 2D + overlays ─────────────────────────────
                    switch (scene.stSceneType)
                    {
                            case SceneType::SCENE_INTRO:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering splash screen 2D elements");
                                #endif
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)])
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                                break;
                            }

                            case SceneType::SCENE_INTRO_MOVIE:
                            {
                                RenderIntroMovie();
                                break;
                            }

                            case SceneType::SCENE_GAMETITLE:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering game title 2D elements");
                                #endif
                                if (threadManager.threadVars.bLoaderTaskFinished.load())
                                    myCamera.SetYawPitch(0.240f, -0.28f);
                                fxManager.RenderLoadingText();
                                break;
                            }

                            case SceneType::SCENE_GAMEPLAY:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering gameplay 2D elements");
                                #endif
                                if (!threadManager.threadVars.bLoaderTaskFinished.load() ||
                                    fxManager.HasActiveLoadingTextEffects())
                                {
                                    fxManager.RenderLoadingText();
                                }
                                break;
                            }

                            default:
                                break;
                        }

                    // FPS display + debug info overlay
                    if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo)
                    {
                        static auto lastFrameTimeFPS = std::chrono::steady_clock::now();
                        static auto lastFPSTime      = lastFrameTimeFPS;
                        static int  fpsFrameCounter  = 0;

                        auto  currentTime   = std::chrono::steady_clock::now();
                        float elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count();

                        lastFrameTimeFPS = currentTime;
                        fpsFrameCounter++;

                        if (elapsedForFPS >= 1.0f)
                        {
                            fps            = static_cast<float>(fpsFrameCounter) / elapsedForFPS;
                            fpsFrameCounter = 0;
                            lastFPSTime    = currentTime;
                        }

                        XMFLOAT3 Coords = myCamera.GetPosition();

                        std::wstring fpsText =
                            L"FPS: " + std::to_wstring(fps) +
                            L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) + L", y" + std::to_wstring(myMouseCoords.y) +
                            L"\nClient Width: " + std::to_wstring(iOrigWidth) + L", Client Height:" + std::to_wstring(iOrigHeight) +
                            L"\nCamera X: " + std::to_wstring(Coords.x) + L", Y: " + std::to_wstring(Coords.y) + L", Z: " + std::to_wstring(Coords.z) +
                            L", Yaw: " + std::to_wstring(myCamera.m_yaw) + L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n" +
                            L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n";

                        const float dbgFontSize = std::clamp(height / 108.0f, 8.0f, 12.0f);
                        DrawMyText(fpsText, Vector2(0, 0), MyColor(255, 255, 255, 255), dbgFontSize);
                    }

                    // Renderer info overlay — bottom-right corner
                    if (USE_RENDERER_INFO && !scene.bSceneSwitching && m_d2dContext && m_dwriteFactory)
                    {
                        bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE ||
                                       scene.stSceneType == SceneType::SCENE_GAMEPLAY  ||
                                       scene.stSceneType == SceneType::SCENE_INTRO     ||
                                       scene.stSceneType == SceneType::SCENE_INTRO_MOVIE ||
                                       scene.stSceneType == SceneType::SCENE_GAMEOVER);
                        #if defined(_DEBUG)
                            riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
                        #endif

                        if (riShow)
                        {
                            const float riFontSize = std::clamp(
                                static_cast<float>(iOrigHeight) / 72.0f, 10.0f, 16.0f);

                            const std::wstring riText =
                                std::wstring(GAME_NAME_W L" " PLATFORM_NAME_W L" " RENDERER_NAME_W L" v") +
                                std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                                std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                                std::to_wstring(CURRENT_BUILD);

                            IDWriteTextFormat* riFmt = GetOrCreateTextFormat(FontName, riFontSize);
                            if (riFmt)
                            {
                                ComPtr<IDWriteTextLayout> riLayout;
                                HRESULT riHr = m_dwriteFactory->CreateTextLayout(
                                    riText.c_str(), static_cast<UINT32>(riText.size()),
                                    riFmt,
                                    static_cast<float>(iOrigWidth),
                                    riFontSize * 2.0f,
                                    &riLayout);

                                if (SUCCEEDED(riHr) && riLayout)
                                {
                                    DWRITE_TEXT_METRICS riMetrics = {};
                                    riLayout->GetMetrics(&riMetrics);

                                    const float riX = static_cast<float>(iOrigWidth)  - riMetrics.width;
                                    const float riY = static_cast<float>(iOrigHeight) - riMetrics.height;

                                    if (SetGeneralBrushColor(220.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f))
                                    {
                                        m_d2dContext->DrawTextLayout(
                                            D2D1::Point2F(riX, riY),
                                            riLayout.Get(),
                                            m_generalBrush.Get());
                                    }
                                }
                            }
                        }
                    }

                    // 5-second OSD notification after F2 debug toggle
                    if (bDebugOSDActive)
                    {
                        float osdElapsed = std::chrono::duration<float>(
                            std::chrono::steady_clock::now() - debugOSDStartTime).count();
                        if (osdElapsed < 5.0f)
                        {
                            std::wstring osdMsg = config.myConfig.showDebugInfo
                                ? L"=> Debug Info: ENABLED"
                                : L"=> Debug Info: DISABLED";
                            DrawMyText(osdMsg, Vector2(10.0f, 80.0f), MyColor(255, 220, 0, 255), 14.0f);
                        }
                        else
                            bDebugOSDActive = false;
                    }

                    // Animated loading circle while assets are loading
                    if (!threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        delay++;
                        if (delay > 3) { loadIndex++; if (loadIndex > 9) { loadIndex = 0; } delay = 0; }

                        if (m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)])
                        {
                            iPosX = loadIndex << 5;
                            Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                                iOrigWidth - 34, iOrigHeight - 45, iPosX, 0, 32, 32);
                        }
                    }

                    // 2D effects overlay
                    try {
                        fxManager.Render2D();
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] 2D effects rendering failed: %hs", e.what());
                        #endif
                    }

                    // GUI windows and interface elements
                    try {
                        guiManager.Render();
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] GUI rendering failed: %hs", e.what());
                        #endif
                    }

                    // Mouse cursor (always on top)
                    if (m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)])
                        Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR, myMouseCoords.x, myMouseCoords.y);

                    // Blinking REC indicator when screen recorder is active
                    if (screenRecorder.IsRecording())
                    {
                        static int recBlinkCounter = 0;
                        recBlinkCounter = (recBlinkCounter + 1) % 60;
                        if (recBlinkCounter < 30)
                        {
                            DrawMyText(L"* REC",
                                Vector2(width - 75.0f, 12.0f),
                                MyColor::Red(),
                                18.0f);
                        }
                    }

                    // Full-screen pass FX (color fader, starfield, warp tunnel) — must be inside BeginDraw/EndDraw
                    try {
                        fxManager.Render();
                    }
                    catch (const std::exception& e)
                    {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Post-processing effects failed: %hs", e.what());
                        #endif
                    }

                    // End D2D overlay pass
                    try
                    {
                        HRESULT hr = m_d2dContext->EndDraw();
                        if (FAILED(hr))
                        {
                            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Direct2D EndDraw failed (0x%08X)", hr);
                            #endif
                        }
                    }
                    catch (const std::exception& e)
                    {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] EndDraw failed: %hs", e.what());
                        #endif
                    }

                    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] 2D overlay pass completed");
                    #endif
                }

                // Return D2D context target and release the wrapped resource.
                // ReleaseWrappedResources() transitions the back buffer to PRESENT state
                // ready for PresentFrame().
                m_d2dContext->SetTarget(nullptr);
                m_dx11Dx12Compat.dx11On12Device->ReleaseWrappedResources(&wrappedRes, 1);
                m_dx11Dx12Compat.dx11Context->Flush();
            }

            // STEP 12: Present the finished frame
            try {
                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Presenting frame to display");
                #endif

                // Capture the fully-composed back-buffer BEFORE Present.
                // ReleaseWrappedResources above transitions it to PRESENT state,
                // which is the state CaptureFrame expects to transition from.
                if (screenRecorder.IsRecording())
                    screenRecorder.CaptureFrame(m_d3d12Device.Get(), m_commandQueue.Get(),
                                                m_swapChain.Get(), m_frameIndex);

                std::chrono::steady_clock::time_point presentStart = std::chrono::steady_clock::now();

                PresentFrame();
                MoveToNextFrame();

                // If VSync is disabled apply a conservative software frame cap
                if (!config.myConfig.enableVSync)
                {
                    const auto targetFrame = std::chrono::milliseconds(16);   // ~60 FPS cap
                    auto presentEnd        = std::chrono::steady_clock::now();
                    auto frameTime         = std::chrono::duration_cast<std::chrono::milliseconds>(presentEnd - presentStart);
                    if (frameTime < targetFrame)
                        std::this_thread::sleep_for(targetFrame - frameTime);
                }
            }
            catch (const std::exception& e) {
                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Present operation failed: %hs", e.what());
                #endif
            }

            // STEP 13: Clear rendering state for next frame
            threadManager.threadVars.bIsRendering.store(false);

#ifdef RENDERER_IS_THREAD
        } // End of while loop for threaded rendering
#endif

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Render operation completed successfully");
        #endif
    }
    catch (const std::exception& e)
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[DX12 RENDERFRAME] Critical exception occurred: %hs", e.what());
        #endif

        threadManager.threadVars.bIsRendering.store(false);
    }

#ifdef RENDERER_IS_THREAD
    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 RENDERFRAME] Render thread exiting normally");
    #endif
#endif

    // FINAL: Guarantee rendering state is clear
    threadManager.threadVars.bIsRendering.store(false);
    // exclusiveRenderLock releases automatically on scope exit
}

// ===========================================================================================
// RenderGamePlay — 3D rendering pipeline for gameplay and title scenes.
// ===========================================================================================
inline void DX12Renderer::RenderGamePlay(float deltaTime)
{
    if (!m_commandList || !m_constantBuffer)
        return;

    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering gameplay scene - 3D pipeline");
    #endif

    // Debug pixel shader mode switching (debug builds only)
    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
        if (GetAsyncKeyState('1') & 0x8000) SetDebugMode(0); // Production view mode
        if (GetAsyncKeyState('2') & 0x8000) SetDebugMode(1); // Normals only mode
        if (GetAsyncKeyState('3') & 0x8000) SetDebugMode(2); // Texture only mode
        if (GetAsyncKeyState('4') & 0x8000) SetDebugMode(3); // Lighting only mode
        if (GetAsyncKeyState('5') & 0x8000) SetDebugMode(4); // Specular only mode
        if (GetAsyncKeyState('6') & 0x8000) SetDebugMode(5); // Attenuation/normals mode
        if (GetAsyncKeyState('7') & 0x8000) SetDebugMode(6); // Shadows only mode
        if (GetAsyncKeyState('8') & 0x8000) SetDebugMode(7); // Reflection only mode
        if (GetAsyncKeyState('9') & 0x8000) SetDebugMode(8); // Metallic only mode
    #endif

    // Render all loaded scene models when loading is complete
    if (threadManager.threadVars.bLoaderTaskFinished.load())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering 3D models");
        #endif

        // Set the DX12 PSO once for all model draws this frame
        if (m_pipelineState)
            m_commandList->SetPipelineState(m_pipelineState.Get());

        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            if (scene.scene_models[i].m_isLoaded && !scene.scene_models[i].m_modelInfo.bIsTransformProxy)
            {
                // Push current camera matrices and state into the model info so
                // RenderDX12() can compute the world-view-projection.
                scene.scene_models[i].m_modelInfo.fxActive        = false;
                scene.scene_models[i].m_modelInfo.viewMatrix       = myCamera.GetViewMatrix();
                scene.scene_models[i].m_modelInfo.projectionMatrix = myCamera.GetProjectionMatrix();
                scene.scene_models[i].m_modelInfo.cameraPosition   = myCamera.GetPosition();

                // Draw via the DX12 native command-list path.
                // RenderDX12() binds the upload-heap VB/IB/CB directly on the
                // command list, bypassing the D3D11-shader guard in Render().
                scene.scene_models[i].RenderDX12(m_commandList.Get(), this, deltaTime);
            }
        }
    }
}

// ===========================================================================================
// RenderIntroMovie — 2D movie playback rendering for SCENE_INTRO_MOVIE.
// ===========================================================================================
inline void DX12Renderer::RenderIntroMovie()
{
    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering movie intro 2D elements");
    #endif

    if (!moviePlayer.IsPlaying())
        return;

    moviePlayer.UpdateFrame();
    moviePlayer.Render(Vector2(0, 0), Vector2(iOrigWidth, iOrigHeight));

    // Company logo overlay at half size, bottom-left corner
    if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
    {
        D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
        int halfW = static_cast<int>(logoSz.width  * 0.5f);
        int halfH = static_cast<int>(logoSz.height * 0.5f);
        Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
    }

    // Space bar to skip movie
    if (GetAsyncKeyState(' ') & 0x8000)
    {
        moviePlayer.Stop();
        scene.bSceneSwitching = true;
        fxManager.FadeToBlack(1.0f, 0.06f);
    }
}

// ===========================================================================================
// RenderBackgroundImage — D2D background rendered before any 3D content.
// ===========================================================================================
void DX12Renderer::RenderBackgroundImage()
{
    if (!m_d2dContext || !IsDX11CompatibilityAvailable())
        return;

    // Guard: skip if system is in a state that makes rendering unsafe
    if (threadManager.threadVars.bIsShuttingDown.load() ||
        bIsMinimized.load()                             ||
        threadManager.threadVars.bIsResizing.load()     ||
        !bIsInitialized.load())
        return;

    // Acquire D2D lock — same lock used by the main 2D block
    ThreadLockHelper d2dBgLock(threadManager, D2DLockName, 100);
    if (!d2dBgLock.IsLocked())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RenderBackgroundImage] Could not acquire D2D lock - skipping background");
        #endif
        return;
    }

    m_d2dContext->BeginDraw();

    switch (scene.stSceneType)
    {
        case SceneType::SCENE_GAMETITLE:
        {
            if (threadManager.threadVars.bLoaderTaskFinished.load())
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)])
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);

                // Company logo overlay at half size, bottom-left corner
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
                {
                    D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                    int halfW = static_cast<int>(logoSz.width  * 0.5f);
                    int halfH = static_cast<int>(logoSz.height * 0.5f);
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
                }

                // 3D starfield and warp-dot tunnel are rendered via fxManager once DX12
                // FXManager render paths are available (currently DX11-only).
            }
            else
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                {
                    if (threadManager.threadVars.bInitiateFader.load())
                    {
                        threadManager.threadVars.bInitiateFader.store(false);
                        fxManager.FadeToImage(1.0f, 0.1f);
                    }
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                }
            }
            break;
        }

        case SceneType::SCENE_GAMEPLAY:
        {
            if (!threadManager.threadVars.bLoaderTaskFinished.load())
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                {
                    if (threadManager.threadVars.bInitiateFader.load())
                    {
                        threadManager.threadVars.bInitiateFader.store(false);
                        fxManager.FadeToImage(1.0f, 0.1f);
                    }
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                }
            }
            break;
        }

        #if defined(_DEBUG)
        case SceneType::SCENE_EXPERIMENT:
        {
            // Black background — 3D tunnel dots rendered via DX12 FXManager when available
            break;
        }
        #endif

        default:
            break;
    }

    try
    {
        HRESULT hr = m_d2dContext->EndDraw();
        if (FAILED(hr))
        {
            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RenderBackgroundImage] EndDraw failed (0x%08X)", hr);
            #endif
        }
    }
    catch (const std::exception& e)
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RenderBackgroundImage] EndDraw exception: %hs", e.what());
        #endif
    }
} // End of RenderBackgroundImage()

#pragma warning(pop)

#endif // defined(__USE_DIRECTX_12__)
