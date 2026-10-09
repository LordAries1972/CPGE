/* -------------------------------------------------------------- */
// Main Tasking Thread for our DX12 renderer
//
// This function is the main (optional thread) function that will
// be used to render the scene. It will be responsible for rendering
// the scene, updating the scene, and handling any other rendering
// tasks.
//
// Pipeline order (DX12) -- ONE command list per frame; the back buffer is bounced
// PRESENT -> RENDER_TARGET exactly once, then handed to Direct2D, then to Present:
//  0) Pace: wait on the swap-chain frame-latency waitable (VSync on) at the TOP of the
//     frame so camera / input / delta time are sampled as late as possible.
//  1) Initialize render, safety guards, acquire exclusive lock
//  2) Viewport calculation from client rect
//  3) Camera update + delta time  <- BEFORE the fence wait (CPU-GPU overlap, mirrors Vulkan)
//  4) Wait for this frame slot's previous use (fence), read back its GPU timestamps,
//     reset allocator + command list
//  5) Transition back buffer PRESENT -> RENDER_TARGET, clear RT + DSV
//  6) SCENE_GAMETITLE backdrop (DX12TitlePipeline::RecordBackdrop): background image
//     (with the intro zoom), company logo, 3D starfield, fireworks -- all native D3D12,
//     no Direct2D and no DX11-on-12 interop
//  7) Update + bind constant buffers (per-frame-in-flight slices)
//  8) Scene 3D (RenderGamePlay): planar plan, probe, shadows, planar mirror, live capture, models
//  9) SCENE_GAMETITLE TSOO logo with the fade-strobe alpha (native), then Close + Execute
// 10) Direct2D overlay recorded straight onto the back buffer via DX11-on-12: loading image,
//     2D FX, text, GUI, cursor, FPS.  Release + Flush leave the back buffer in PRESENT.
// 11) Closer list (only when needed): GPU timestamp resolve (debug capture) and/or the
//     RENDER_TARGET -> PRESENT transition if the D2D overlay was unavailable this frame
// 12) Present (non-blocking with the waitable).  MoveToNextFrame() signals the frame fence.
//     (VSync=on: paced to the display by STEP 0; VSync=off: uncapped)

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
#include "FXManager.h"
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
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RENDERFRAME] Another render operation already active - aborting");
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

            // CRITICAL RESOURCE CHECK — recover from lost swap chain or command queue
            if (!m_swapChain || !m_commandQueue || !m_fence)
            {
                if (!threadManager.threadVars.bIsResizing.load() && !sysUtils.IsWindowMinimized())
                {
                    debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RENDERFRAME] Critical resources invalid. Attempting recovery.");
                    threadManager.threadVars.bIsResizing.store(true);
                    try {
                        Resize(iOrigWidth, iOrigHeight);
                        ResumeLoader();
                    }
                    catch (const std::exception& e) {
                        debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Recovery failed: %hs", e.what());
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

            // STEP 1: Viewport dimensions from the renderer's actual back-buffer size.
            // iOrigWidth/iOrigHeight are set by Resize() and always match the swap-chain back buffer,
            // whether in windowed or fullscreen exclusive mode.
            // Previously, fullscreen used winMetrics.monitorFullArea (GetMonitorInfo.rcMonitor),
            // which reflects OS monitor-space coordinates and can be stale after a mode switch
            // (e.g. native resolution reported while swap chain is at a non-native configured res),
            // making the 3D viewport disagree with D2D / 2D content that always draws at
            // iOrigWidth x iOrigHeight.  Using these members keeps both coordinate spaces in sync.
            float width  = static_cast<float>(iOrigWidth);   // Swap-chain back-buffer width
            float height = static_cast<float>(iOrigHeight);  // Swap-chain back-buffer height

            viewport.Width    = width;
            viewport.Height   = height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;

            scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

            // STEP 0: Frame pacing.  The swap chain was created with a frame-latency waitable object
            // (MaxFrameLatency = BufferCount - 1).  Waiting on it at the TOP of the frame - rather than
            // after Present - means input, camera and delta time are sampled after the display has
            // released a slot, i.e. as late as possible, instead of a whole frame early.
            // Gated on VSync so VSync-off / ALLOW_TEARING stays genuinely uncapped.
            #if defined(_DEBUG)
                const auto paceStart = std::chrono::steady_clock::now();
            #endif
            if (m_frameLatencyWaitableObject && config.myConfig.enableVSync)
                WaitForSingleObjectEx(m_frameLatencyWaitableObject, 1000, TRUE);
            #if defined(_DEBUG)
                const double paceWaitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - paceStart).count();
            #endif

            // STEP 3: Camera update + delta time — computed BEFORE the GPU fence wait
            // so the CPU does useful work while WaitForPreviousFrame() may stall.
            // Mirrors the Vulkan render loop where delta/camera are computed before
            // vkWaitForFences to maximise CPU-GPU overlap.
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

            #if defined(_DEBUG)
                const bool bCollectTiming = (scene.stSceneType == SceneType::SCENE_GAMETITLE) && IsTimingCaptureActive();
                const auto timingFrameStart = paceStart;                    // Total includes the pace wait: it is the real frame period
                Renderer::RenderTimingSample timingSample = {};
                auto timingMs = [](const std::chrono::steady_clock::time_point& start,
                                   const std::chrono::steady_clock::time_point& end) -> double {
                    return std::chrono::duration<double, std::milli>(end - start).count();
                };
            #endif

            // STEP 2: Wait for the previous frame, then reset the command list
            #if defined(_DEBUG)
                auto timingPhaseStart = std::chrono::steady_clock::now();
            #endif
            WaitForPreviousFrame();
            #if defined(_DEBUG)
                if (bCollectTiming)
                    timingSample.waitPreviousMs = paceWaitMs + timingMs(timingPhaseStart, std::chrono::steady_clock::now());
                timingPhaseStart = std::chrono::steady_clock::now();
            #endif
            ResetCommandList();
            #if defined(_DEBUG)
                if (bCollectTiming)
                    timingSample.resetMs = timingMs(timingPhaseStart, std::chrono::steady_clock::now());

                // GPU timestamps.  The fence wait above proved this frame slot's PREVIOUS use has finished on
                // the GPU, so its stamps can be read back now; then start this frame's stamp sequence.
                {
                    DX12GpuProfiler::Result gpuPrev;
                    if (m_gpuProf.Collect(m_frameIndex, gpuPrev) && bCollectTiming)
                    {
                        timingSample.gpuValid         = true;
                        timingSample.gpuTotalMs       = gpuPrev.totalMs;
                        timingSample.gpuBackdropMs    = gpuPrev.backdropMs;
                        timingSample.gpuShadowsMs     = gpuPrev.shadowsMs;
                        timingSample.gpuReflectionsMs = gpuPrev.reflectionsMs;
                        timingSample.gpuModelsMs      = gpuPrev.modelsMs;
                        timingSample.gpuTsooMs        = gpuPrev.tsooMs;
                        timingSample.gpuOverlayMs     = gpuPrev.overlayMs;
                    }
                    m_gpuProf.BeginFrame(m_frameIndex, bCollectTiming);
                    m_gpuProf.StampUpTo(m_commandList.Get(), DX12GpuProfiler::STAMP_FRAME_START);
                }
                timingPhaseStart = std::chrono::steady_clock::now();
            #endif

            // STEP 3.5: Native title pipeline gate - SCENE_GAMETITLE only, once loading is complete.
            //
            // The old design wrapped the real 3D pass in a Direct2D "pre-pass" (full-screen bitmap blit,
            // an extra Close/Execute, two DX11-on-12 Acquire/Release/Flush round trips per frame).  The
            // background, logo, starfield, fireworks and TSOO logo are now plain D3D12 draws recorded on
            // THIS command list by DX12TitlePipeline.  Direct2D only draws the text / GUI overlay (STEP 10).
            // If a title texture failed to load, that one element falls back to the D2D overlay.
            // bLoaderTaskFinished only ever goes false -> true, and the flip happens in an FX callback run from
            // the D2D pass below on this same thread.  Read it once so the whole frame sees one value.
            const bool bLoaderDone = threadManager.threadVars.bLoaderTaskFinished.load();
            const bool bTitleNative =
                scene.stSceneType == SceneType::SCENE_GAMETITLE              &&
                m_title.IsReady()                                            &&
                bLoaderDone                                                  &&
                (!threadManager.threadVars.bIsShuttingDown.load())           &&
                (!bIsMinimized.load())                                       &&
                (!threadManager.threadVars.bIsResizing.load())               &&
                bIsInitialized.load();
            const bool bNativeBackdropDrawn = bTitleNative && m_title.HasSlot(DX12TitlePipeline::kSlotBackground);
            const bool bNativeTSOODrawn     = bTitleNative && m_title.HasSlot(DX12TitlePipeline::kSlotTSOO);

            #if defined(_DEBUG)
                timingPhaseStart = std::chrono::steady_clock::now();
            #endif

            // STEP 4: Set root signature, descriptor heaps, viewport, scissor
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
            ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
            m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
            m_commandList->RSSetViewports(1, &viewport);
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // STEP 5: Transition back buffer PRESENT -> RENDER_TARGET.  This is the only transition on the
            // way in: the 11On12 wrapped back buffer (InState=RENDER_TARGET) is acquired in this state in
            // STEP 10 and released straight to PRESENT.
            TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

            // MainRTV(): the multisampled colour target while MSAA is on (resolved into the back buffer at STEP 8.9),
            // otherwise the back buffer itself.  The depth buffer shares the same sample count.
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(MainRTV(m_frameIndex));
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);

            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

            // STEP 6: Clear render targets (no lock needed - render thread is the sole user).
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

            // STEP 6.5: Native title backdrop.  BACKGROUND -> LOGO -> STARFIELD -> FIREWORKS, drawn before the
            // 3D models so the ship composites on top.  The TSOO logo follows the models (STEP 8.5).
            if (bNativeBackdropDrawn)
            {
                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] STEP 6.5: native title backdrop (SCENE_GAMETITLE)");
                #endif

                m_title.RecordBackdrop(m_commandList.Get(), m_frameIndex);

                // Restore the 3D model pipeline state the sprite draws overwrote.
                m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
                ID3D12DescriptorHeap* ppHeapsRestore[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
                m_commandList->SetDescriptorHeaps(_countof(ppHeapsRestore), ppHeapsRestore);
                m_commandList->SetPipelineState(MainPSO());
                m_commandList->RSSetViewports(1, &viewport);
                m_commandList->RSSetScissorRects(1, &scissorRect);
                m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
            }

            #if defined(_DEBUG)
                if (bCollectTiming)
                    timingSample.backgroundPrePassMs = timingMs(timingPhaseStart, std::chrono::steady_clock::now());
                m_gpuProf.StampUpTo(m_commandList.Get(), DX12GpuProfiler::STAMP_BACKDROP);
            #endif

            // Animate lights (pulse / flicker / strobe) each frame before updating
            // the global light buffer — mirrors the DX11, OpenGL, and Vulkan render paths.
            lightsManager.AnimateLights(deltaTime);

            // STEP 7: Update + bind constant buffers via root descriptors
            UpdateConstantBuffers();

            if (m_constantBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER,
                    CameraCBAddress());

            if (m_globalLightBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER,
                    GlobalLightCBAddress());

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
                    break;
                    
                case SceneType::SCENE_INTRO_MOVIE:
                    break;

                case SceneType::SCENE_GAMETITLE:
                {
                    if (threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        int iModelID = scene.FindParentModelID(SplashShipName);
                        if (scene.modelAnimator.IsAnimationPlaying(iModelID))
                            scene.modelAnimator.UpdateAnimations(deltaTime);
                        RenderGamePlay(deltaTime);
                    }
                    break;
                }

                case SceneType::SCENE_GAMEPLAY:
                {
                    int iModelID = scene.FindParentModelID(ShipName1);
                    if (scene.modelAnimator.IsAnimationPlaying(iModelID))
                        scene.modelAnimator.UpdateAnimations(deltaTime);
                    RenderGamePlay(deltaTime);
                    break;
                }

                default:
                    break;
            }

            // STEP 8.5: TSOO logo (centred, fade-strobe alpha) over the 3D models - native.
            if (bNativeTSOODrawn)
                m_title.RecordTSOO(m_commandList.Get(), m_frameIndex);

            #if defined(_DEBUG)
                m_gpuProf.StampUpTo(m_commandList.Get(), DX12GpuProfiler::STAMP_TSOO);
                if (bCollectTiming)
                    timingSample.commandRecordMs = timingMs(timingPhaseStart, std::chrono::steady_clock::now()) - timingSample.backgroundPrePassMs;
                timingPhaseStart = std::chrono::steady_clock::now();
            #endif

            // STEP 8.9: MSAA resolve.  Everything above (backdrop, models, TSOO logo) went into the multisampled
            // target; resolve it into the back buffer so the Direct2D overlay (STEP 10) draws on the final image.
            // The back buffer is left in RENDER_TARGET, as STEP 9 requires.
            ResolveMsaaToBackBuffer(m_commandList.Get());

            // STEP 9: Close and execute 3D command list.
            // Deliberately leave the back buffer in RENDER_TARGET state so that
            // AcquireWrappedResources (inState=RENDER_TARGET) can hand it to D2D.
            CloseCommandList();
            ExecuteCommandList();
            #if defined(_DEBUG)
                if (bCollectTiming)
                    timingSample.execute3DMs = timingMs(timingPhaseStart, std::chrono::steady_clock::now());
                timingPhaseStart = std::chrono::steady_clock::now();
            #endif

            // STEP 10: Direct2D overlay via DX11On12 - text, GUI, 2D FX, cursor, loading image.
            //
            // D2D renders directly into the wrapped swap-chain back buffer (left in RENDER_TARGET by
            // STEP 9).  ReleaseWrappedResources with OutState=PRESENT transitions it for Present, and
            // Flush submits the interop commands to the shared queue.  Track whether that happened:
            // if the overlay is unavailable this frame the closer list (STEP 11) does the transition.
            bool bBackBufferToPresent = false;
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
                            if (bNativeBackdropDrawn)
                            {
                                // Background / logo / starfield / fireworks were drawn natively in STEP 6.5.
                            }
                            else if (bLoaderDone)
                            {
                                // Native backdrop unavailable (background texture failed to load, or the native
                                // pipeline could not be created) - D2D fallback.  Drawn after the 3D models.
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)]) {
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                                }

                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
                                    D2D1_SIZE_F sz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                                    int fbW = static_cast<int>(sz.width * 0.5f);
                                    int fbH = static_cast<int>(sz.height * 0.5f);
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - fbH, fbW, fbH);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - fbH, fbW, fbH);
                                }
                            }
                            else
                            {
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                                {
                                    // Consume (but no longer act on) the legacy black-fade-in trigger --
                                    // the end-of-load pixel fader now owns the loading-screen reveal.
                                    if (threadManager.threadVars.bInitiateFader.load())
                                        threadManager.threadVars.bInitiateFader.store(false);
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                                }
                            }

                            // TSOO logo fallback: only when the native TSOO texture is unavailable.
                            if (bLoaderDone && !bNativeTSOODrawn && m_d2dTextures[int(BlitObj2DIndexType::IMG_TSOO)])
                            {
                                int startX = (iOrigWidth - 536) / 2;                // Centered horizontally
                                int startY = (iOrigHeight - 466) / 2;               // Centered vertically
                                if (fxManager.IsImageFadeStrobeActive(BlitObj2DIndexType::IMG_TSOO))
                                    fxManager.RenderImageFadeStrobe(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);
                                else
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);
                            }
                            break;
                        }

                        case SceneType::SCENE_GAMEPLAY:
                        {
                            if (!bLoaderDone &&
                                m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                            {
                                // Consume (but no longer act on) the legacy black-fade-in trigger --
                                // the end-of-load pixel fader now owns the loading-screen reveal.
                                if (threadManager.threadVars.bInitiateFader.load())
                                    threadManager.threadVars.bInitiateFader.store(false);
                                if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                                    fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                                else
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                            }
                            break;
                        }

                        default:
                            break;
                    }

                    // Background FX (starfield, warp tunnel) behind 3D content.
                    // When the STEP 6.5 native backdrop ran it already drew (and updated) these BEFORE
                    // the 3D models - rendering them again here would paint the stars over the ship
                    // model and advance their simulation twice per frame.
                    if (!bNativeBackdropDrawn)
                    {
                        try { fxManager.Render(true); }
                        catch (const std::exception&) {
                            // Per-frame path: intentionally silent (FXManager::Render already logs its own exceptions).
                        }
                    }

                    // ── Scene-specific 2D + overlays ─────────────────────────────
                    switch (scene.stSceneType)
                    {
                            case SceneType::SCENE_INTRO:
                            {
                                if (moviePlayer.IsPlaying())
                                    RenderIntroMovie();
                                /*if (m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)]) {
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_SPLASH1)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_SPLASH1), 0, 0, iOrigWidth, iOrigHeight);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                                } */
                                break;
                            }

                            case SceneType::SCENE_INTRO_MOVIE:
                            {
                                if (moviePlayer.IsPlaying())
                                    RenderIntroMovie();
                                break;
                            }

                            case SceneType::SCENE_GAMETITLE:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering game title 2D elements");
                                #endif
                                fxManager.RenderLoadingText();
                                break;
                            }

                            case SceneType::SCENE_GAMEPLAY:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering gameplay 2D elements");
                                #endif
                                if (!bLoaderDone || fxManager.HasActiveLoadingTextEffects())
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
                        // Reuse the per-frame timestamp already captured for delta-time —
                        // avoids a second steady_clock::now() call per frame.
                        static auto lastFPSTime     = now;
                        static int  fpsFrameCounter = 0;

                        float elapsedForFPS = std::chrono::duration<float>(now - lastFPSTime).count();
                        fpsFrameCounter++;

                        if (elapsedForFPS >= 1.0f)
                        {
                            fps             = static_cast<float>(fpsFrameCounter) / elapsedForFPS;
                            fpsFrameCounter = 0;
                            lastFPSTime     = now;
                        }

                        #ifdef _DEBUG
                        // Debug builds: full diagnostic overlay
                        const XMFLOAT3 Coords = myCamera.GetPosition();
                        wchar_t fpsBuf[512];
                        swprintf_s(fpsBuf, _countof(fpsBuf),
                            L"FPS: %.2f\nMOUSE: x%.0f, y%.0f"
                            L"\nClient Width: %d, Client Height:%d"
                            L"\nCamera X: %.3f, Y: %.3f, Z: %.3f, Yaw: %.3f, Pitch: %.3f"
                            L"\nGlobal Light Count: %d\n",
                            fps,
                            myMouseCoords.x, myMouseCoords.y,
                            iOrigWidth, iOrigHeight,
                            Coords.x, Coords.y, Coords.z,
                            myCamera.m_yaw, myCamera.m_pitch,
                            lightsManager.GetLightCount());
                        #else
                        // Release builds: FPS only
                        wchar_t fpsBuf[32];
                        swprintf_s(fpsBuf, _countof(fpsBuf), L"FPS: %.2f", fps);
                        #endif

                        const float dbgFontSize = std::clamp(height / 108.0f, 8.0f, 12.0f);
                        DrawMyText(fpsBuf, Vector2(5.0f, 5.0f), MyColor(255, 255, 255, 255), dbgFontSize);
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

                            // e.g. "Debug CPGE v0.0.1723 15-06-2026"
                            static const std::wstring buildDate = []() -> std::wstring {
                                const char* d = __DATE__; // "Mmm DD YYYY", e.g. "Jun 15 2026"
                                const char* m = "JanFebMarAprMayJunJulAugSepOctNovDec";
                                int mon = 1;
                                for (int i = 0; i < 12; i++) {
                                    if (d[0]==m[i*3] && d[1]==m[i*3+1] && d[2]==m[i*3+2]) { mon=i+1; break; }
                                }
                                int day  = (d[4]==' ') ? (d[5]-'0') : ((d[4]-'0')*10+(d[5]-'0'));
                                int year = (d[7]-'0')*1000+(d[8]-'0')*100+(d[9]-'0')*10+(d[10]-'0');
                                wchar_t buf[12];
                                swprintf_s(buf, 12, L"%02d-%02d-%04d", day, mon, year);
                                return std::wstring(buf);
                            }();
                            // Static: all components are compile-time constants or the
                            // already-static buildDate — computed once, zero heap allocs thereafter.
                            static const std::wstring riText =
                                std::wstring(BUILD_TYPE_W L" " RENDERER_NAME_W L" " GAME_NAME_W L" v") +
                                std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                                std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                                std::to_wstring(CURRENT_BUILD)            + L" " +
                                buildDate;

                            IDWriteTextFormat* riFmt = GetOrCreateTextFormat(FontName, riFontSize);
                            if (riFmt)
                            {
                                // Cache the text layout — the text and font are both static, so
                                // creating it fresh every frame is a pointless per-frame COM alloc.
                                // Invalidated when the font size changes (window resize).
                                static ComPtr<IDWriteTextLayout> s_riLayout;
                                static float s_riLayoutFontSize = 0.0f;
                                if (!s_riLayout || s_riLayoutFontSize != riFontSize)
                                {
                                    s_riLayout.Reset();
                                    if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(
                                            riText.c_str(), static_cast<UINT32>(riText.size()),
                                            riFmt,
                                            static_cast<float>(iOrigWidth),
                                            riFontSize * 2.0f,
                                            &s_riLayout)))
                                        s_riLayoutFontSize = riFontSize;
                                }

                                if (s_riLayout)
                                {
                                    DWRITE_TEXT_METRICS riMetrics = {};
                                    s_riLayout->GetMetrics(&riMetrics);

                                    const float riX = static_cast<float>(iOrigWidth)  - riMetrics.width;
                                    const float riY = static_cast<float>(iOrigHeight) - riMetrics.height;

                                    if (SetGeneralBrushColor(220.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f))
                                    {
                                        m_d2dContext->DrawTextLayout(
                                            D2D1::Point2F(riX, riY),
                                            s_riLayout.Get(),
                                            m_generalBrush.Get());
                                    }
                                }
                            }
                        }
                    }

                    // 5-second OSD notification after F2 debug toggle
                    if (bDebugOSDActive)
                    {
                        // Reuse the per-frame `now` timestamp — avoids a redundant steady_clock::now() call.
                        float osdElapsed = std::chrono::duration<float>(now - debugOSDStartTime).count();
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

                    #if defined(_DEBUG)
                    // F12 timing capture notification.  Kept separate from the F2
                    // debug-info OSD so the user gets immediate feedback when the
                    // 25-frame circular timing buffer is armed or dumped.
                    if (bTimingOSDActive)
                    {
                        float timingOsdElapsed = std::chrono::duration<float>(now - timingOSDStartTime).count();
                        if (timingOsdElapsed < 5.0f)
                            DrawMyText(timingOSDMessage, Vector2(10.0f, 104.0f), MyColor(255, 220, 0, 255), 14.0f);
                        else
                            bTimingOSDActive = false;
                    }
                    #endif

                    // Animated loading circle while assets are loading
                    if (!bLoaderDone)
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
                        debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] 2D effects rendering failed: %hs", e.what());
                    }

                    // GUI windows and interface elements
                    try {
                        guiManager.Render();
                    }
                    catch (const std::exception& e) {
                        debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] GUI rendering failed: %hs", e.what());
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
                        debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Post-processing effects failed: %hs", e.what());
                    }

                    // End D2D overlay pass
                    try
                    {
                        HRESULT hr = m_d2dContext->EndDraw();
                        if (FAILED(hr))
                        {
                            debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Direct2D EndDraw failed (0x%08X)", hr);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] EndDraw failed: %hs", e.what());
                    }

                    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] 2D overlay pass completed");
                    #endif
                }

                // Return D2D context target and release the wrapped resource.
                m_d2dContext->SetTarget(nullptr);
                m_dx11Dx12Compat.dx11On12Device->ReleaseWrappedResources(&wrappedRes, 1);
                // Flush submits the DX11On12 D2D commands (including the back-buffer RENDER_TARGET ->
                // PRESENT transition) to the shared DX12 queue, ahead of the closer list and Present.
                m_dx11Dx12Compat.dx11Context->Flush();
                bBackBufferToPresent = true;
            }

            // STEP 11: Closer list.  Needed when (a) the D2D overlay did not run, so the back buffer is
            // still in RENDER_TARGET and must reach PRESENT before Present, and/or (b) a GPU timestamp
            // capture is active and the final stamp + readback resolve must follow the D2D work on the queue.
            {
                bool bNeedCloser = !bBackBufferToPresent;
                #if defined(_DEBUG)
                    bNeedCloser = bNeedCloser || m_gpuProf.Active();
                #endif

                if (bNeedCloser &&
                    SUCCEEDED(m_commandList->Reset(m_frameContexts[m_frameIndex].compositeAllocator.Get(), nullptr)))
                {
                    if (!bBackBufferToPresent)
                        TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                            D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PRESENT);
                    #if defined(_DEBUG)
                        m_gpuProf.Resolve(m_commandList.Get());
                    #endif
                    CloseCommandList();
                    ExecuteCommandList();
                }
            }
            #if defined(_DEBUG)
                if (bCollectTiming)
                {
                    timingSample.d2dOverlayMs = timingMs(timingPhaseStart, std::chrono::steady_clock::now());
                    timingSample.backgroundPrePass = false;                 // Legacy D2D pre-pass no longer exists; bg=0 confirms the native path
                    timingSample.d2dAvailable = bBackBufferToPresent;
                    timingSample.screenRecorderActive = screenRecorder.IsRecording();
                }
                timingPhaseStart = std::chrono::steady_clock::now();
            #endif

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

                PresentFrame();
                // Frame pacing now happens at the top of the next frame (STEP 0), so Present() stays non-blocking.
                #if defined(_DEBUG)
                    if (bCollectTiming)
                        timingSample.presentMs = timingMs(timingPhaseStart, std::chrono::steady_clock::now());
                    timingPhaseStart = std::chrono::steady_clock::now();
                #endif
                MoveToNextFrame();
                #if defined(_DEBUG)
                    if (bCollectTiming)
                    {
                        const auto timingFrameEnd = std::chrono::steady_clock::now();
                        timingSample.moveNextMs = timingMs(timingPhaseStart, timingFrameEnd);
                        timingSample.totalMs = timingMs(timingFrameStart, timingFrameEnd);
                        RecordTimingSample(timingSample);
                    }
                #endif
            }
            catch (const std::exception& e) {
                debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Present operation failed: %hs", e.what());
            }

            // STEP 13: Clear rendering state for next frame
            threadManager.threadVars.bIsRendering.store(false);
        } // End of while loop for threaded rendering

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Render operation completed successfully");
        #endif
    }
    catch (const std::exception& e)
    {
        debug.logDiagMessage(LogLevel::LOG_CRITICAL, L"[DX12 RENDERFRAME] Critical exception occurred: %hs", e.what());

        threadManager.threadVars.bIsRendering.store(false);
    }

    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 RENDERFRAME] Render thread exiting normally");
    #endif

    // FINAL: Guarantee rendering state is clear
    threadManager.threadVars.bIsRendering.store(false);
    // exclusiveRenderLock releases automatically on scope exit
}

// ===========================================================================================
// RenderShadowPassDX12 — shadow depth passes + per-frame shadow binding.
// Records depth-only draws into the t8 / t9 shadow maps on the open command list, then
// restores the main 3D pass state (root signature, heaps, viewport/scissor, RTV/DSV and
// the frame-level root CBVs b0/b3/b5) and binds b6 (this frame's ShadowBufferData slice)
// plus the t8/t9 descriptor table.  Plans against m_frameGlobalLights - the exact vector
// UpdateConstantBuffers() uploaded to b3 - so lightShadowInfo[i] matches globalLights[i].
// ===========================================================================================
void DX12Renderer::RenderShadowPassDX12()
{
    ID3D12GraphicsCommandList* cl = m_commandList.Get();
    if (!cl) return;

    m_shadowFrame.enabled = false;
    m_shadowFrame.views.clear();

    if (m_shadowResourcesReady && m_shadowPSO && threadManager.threadVars.bLoaderTaskFinished.load())
    {
        // Gather world-space bounding spheres of every shadow-casting mesh.
        m_shadowCasters.clear();
        m_shadowCasterModels.clear();
        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            Model& m = scene.scene_models[i];
            if (!m.m_isLoaded || m.bIsDestroyed) continue;
            ModelInfo& mi = m.m_modelInfo;
            if (mi.bIsTransformProxy || mi.bIsTransformOnly || !mi.castShadows) continue;
            if (!mi.d3d12VertexBuffer || !mi.d3d12IndexBuffer || mi.d3d12IndexCount == 0) continue;
            if (!ModelComputeShadowBounds(mi)) continue;

            XMFLOAT4X4 w;
            XMStoreFloat4x4(&w, mi.worldMatrix);
            const float scale[3] = { mi.scale.x, mi.scale.y, mi.scale.z };
            ShadowCaster c{};
            ShadowMakeWorldSphere(mi.shadowBoundsCenter, mi.shadowBoundsRadius, scale, &w._11, c);
            m_shadowCasters.push_back(c);
            m_shadowCasterModels.push_back(i);
        }

        const XMFLOAT3 cp = myCamera.GetPosition();
        const float camPos[3] = { cp.x, cp.y, cp.z };
        BuildShadowFrame(m_frameGlobalLights, m_shadowCasters, camPos, m_shadowFrame);
    }

    if (m_shadowFrame.enabled)
    {
        // Shadow maps rest in PIXEL_SHADER_RESOURCE between frames.
        D3D12_RESOURCE_BARRIER toWrite[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_shadowDirTex.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_shadowLocalTex.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
        };
        cl->ResourceBarrier(2, toWrite);

        cl->SetGraphicsRootSignature(m_shadowRootSignature.Get());
        cl->SetPipelineState(m_shadowPSO.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (const ShadowView& view : m_shadowFrame.views)
        {
            const bool isDir = (view.target == SHADOW_TARGET_DIRECTIONAL);
            const INT  dsvIndex = isDir ? 0 : (1 + view.target);
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_shadowDsvHeap.cpuStart, dsvIndex, m_shadowDsvHeap.handleIncrementSize);
            const int size = isDir ? m_shadowDirSize : m_shadowLocalSize;

            cl->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
            cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            D3D12_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
            D3D12_RECT     sr = { 0, 0, static_cast<LONG>(size), static_cast<LONG>(size) };
            cl->RSSetViewports(1, &vp);
            cl->RSSetScissorRects(1, &sr);

            for (size_t c = 0; c < m_shadowCasters.size(); ++c)
            {
                if (!ShadowViewAffectsCaster(view, m_shadowCasters[c])) continue;
                ModelInfo& mi = scene.scene_models[m_shadowCasterModels[c]].m_modelInfo;

                XMFLOAT4X4 w;
                XMStoreFloat4x4(&w, mi.worldMatrix);
                float wlvp[16];
                ShadowMat4Mul(&w._11, view.viewProj, wlvp);

                float constants[20];
                for (int r = 0; r < 4; ++r)                                     // transpose for HLSL column_major
                    for (int col = 0; col < 4; ++col)
                        constants[col * 4 + r] = wlvp[r * 4 + col];
                constants[16] = mi.scale.x; constants[17] = mi.scale.y; constants[18] = mi.scale.z; constants[19] = 1.0f;
                cl->SetGraphicsRoot32BitConstants(0, 20, constants, 0);

                cl->IASetVertexBuffers(0, 1, &mi.d3d12VBView);
                cl->IASetIndexBuffer(&mi.d3d12IBView);
                cl->DrawIndexedInstanced(mi.d3d12IndexCount, 1, 0, 0, 0);
            }
        }

        D3D12_RESOURCE_BARRIER toRead[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_shadowDirTex.Get(),
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_shadowLocalTex.Get(),
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        };
        cl->ResourceBarrier(2, toRead);

        // --- Restore the main 3D pass (mirrors RenderFrame STEP 4 / 5 / 7) ---
        cl->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
        cl->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        D3D12_VIEWPORT mainVP = { 0.0f, 0.0f, static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight), 0.0f, 1.0f };
        D3D12_RECT     mainSR = { 0, 0, static_cast<LONG>(iOrigWidth), static_cast<LONG>(iOrigHeight) };
        cl->RSSetViewports(1, &mainVP);
        cl->RSSetScissorRects(1, &mainSR);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(MainRTV(m_frameIndex));
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);
        cl->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        if (m_constantBuffer)
            cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER, CameraCBAddress());
        if (m_globalLightBuffer)
            cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER, GlobalLightCBAddress());
        if (m_envBuffer)
            cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_ENVIRONMENT_BUFFER, m_envBuffer->GetGPUVirtualAddress());

        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    // b6: this frame's slice (always written; shadows off => useShadowMap = useLocalShadows = 0).
    if (m_shadowBuffer && m_shadowBufferMapped && m_shadowBufferStride > 0)
    {
        ShadowBufferData sb;
        ShadowPackGPU(m_shadowFrame, true, sb);
        const UINT64 offset = static_cast<UINT64>(m_frameIndex) * m_shadowBufferStride;
        memcpy(m_shadowBufferMapped + offset, &sb, sizeof(ShadowBufferData));

        cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_SHADOW_BUFFER,
            m_shadowBuffer->GetGPUVirtualAddress() + offset);

        // Second slice with planar reflections off, for the live capture pass: the per-model material buffers are
        // read when the list executes (so reflectors would still carry planar strength), and the capture draws bind
        // a NULL planar array.
        sb.planarParams[0] = 0.0f;
        const UINT64 noPlanarOffset = (static_cast<UINT64>(FrameCount) + m_frameIndex) * m_shadowBufferStride;
        memcpy(m_shadowBufferMapped + noPlanarOffset, &sb, sizeof(ShadowBufferData));
    }

    // t8 / t9 table (null SRVs if shadow resources failed so the root parameter is never unset).
    if (m_shadowSRVTable.ptr != 0)
        cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, CurrentShadowTableDX12());
    else if (m_nullTextureGPUHandle.ptr != 0)
        cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, m_nullTextureGPUHandle);
}

// ===========================================================================================
// CurrentShadowTableDX12 — the main t8-t11 descriptor table; the "live" variant (t10 = live capture cube)
// once a capture cycle has completed, otherwise the sky-probe table.
// ===========================================================================================
D3D12_GPU_DESCRIPTOR_HANDLE DX12Renderer::CurrentShadowTableDX12() const
{
    if (g_reflectionCapture.ready && config.myConfig.reflectionLive && m_capTex && m_shadowSRVTableLive.ptr != 0)
        return m_shadowSRVTableLive;
    return m_shadowSRVTable;
}

// ===========================================================================================
// RestoreMainPassDX12 — re-establishes everything the main model pass relies on.  Needed after a pass that
// switched the root signature (capture mip generation) because that invalidates every root argument.
// Mirrors RenderShadowPassDX12's restore block + the frame-level root CBVs and the b6 / t8-t11 binding.
// ===========================================================================================
void DX12Renderer::RestoreMainPassDX12()
{
    ID3D12GraphicsCommandList* cl = m_commandList.Get();
    if (!cl) return;

    cl->SetGraphicsRootSignature(m_rootSignature.Get());
    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
    cl->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    if (m_pipelineState)
        cl->SetPipelineState(MainPSO());                                        // main pass => the MSAA PSO when MSAA is on

    D3D12_VIEWPORT mainVP = { 0.0f, 0.0f, static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight), 0.0f, 1.0f };
    D3D12_RECT     mainSR = { 0, 0, static_cast<LONG>(iOrigWidth), static_cast<LONG>(iOrigHeight) };
    cl->RSSetViewports(1, &mainVP);
    cl->RSSetScissorRects(1, &mainSR);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(MainRTV(m_frameIndex));
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);
    cl->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    if (m_constantBuffer)
        cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER, CameraCBAddress());
    if (m_globalLightBuffer)
        cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER, GlobalLightCBAddress());
    if (m_envBuffer)
        cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_ENVIRONMENT_BUFFER, m_envBuffer->GetGPUVirtualAddress());
    if (m_shadowBuffer && m_shadowBufferMapped && m_shadowBufferStride > 0)
        cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_SHADOW_BUFFER,
            m_shadowBuffer->GetGPUVirtualAddress() + static_cast<UINT64>(m_frameIndex) * m_shadowBufferStride);
    if (m_shadowSRVTable.ptr != 0)
        cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, CurrentShadowTableDX12());
    else if (m_nullTextureGPUHandle.ptr != 0)
        cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, m_nullTextureGPUHandle);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// ===========================================================================================
// RenderReflectionCaptureDX12 — live scene capture (see "Live scene capture" in Lights.h).
// ONE face per frame: the sky probe's face is copied into the capture cube, every model is drawn over it
// with a 90 degree camera (main root signature + PSO; b0 slot 1 + MAX_PLANAR_PLANES + face; the mirror
// descriptor table so the cube is never referenced while it is a render target; the planar-off b6 slice),
// and after the sixth face the mips are rebuilt with the downsample PSO.  Runs after the planar pass.
// ===========================================================================================
void DX12Renderer::RenderReflectionCaptureDX12(float deltaTime)
{
    ID3D12GraphicsCommandList* cl = m_commandList.Get();
    if (!cl || !g_reflectionFrame.active || !m_reflTex || m_shadowSRVTable.ptr == 0 || m_planarMirrorTable.ptr == 0) return;

    if (!config.myConfig.reflectionLive || !threadManager.threadVars.bLoaderTaskFinished.load())
    {
        g_reflectionCapture.ready = false;
        return;
    }
    if (!m_capTex && !m_capFailed)
        CreateCaptureResourcesDX12();
    if (!m_capTex || m_capSize != m_reflProbe.size || m_reflUploadedVersion == 0) return;

    const XMFLOAT3 cp = myCamera.GetPosition();
    const float camPos[3] = { cp.x, cp.y, cp.z };
    int  face = 0;
    bool lastFace = false;
    if (!ReflectionCaptureNext(deltaTime, camPos, face, lastFace))
        return;

    const UINT mips = static_cast<UINT>(m_capMips);
    const UINT size = static_cast<UINT>(m_capSize);
    const UINT sub  = static_cast<UINT>(face) * mips;                           // mip 0 of this face

    // ---- 1. sky face -> capture face ----
    D3D12_RESOURCE_BARRIER toCopy[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_reflTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE, sub),
        CD3DX12_RESOURCE_BARRIER::Transition(m_capTex.Get(),  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST,   sub) };
    cl->ResourceBarrier(2, toCopy);
    CD3DX12_TEXTURE_COPY_LOCATION dstLoc(m_capTex.Get(), sub);
    CD3DX12_TEXTURE_COPY_LOCATION srcLoc(m_reflTex.Get(), sub);
    cl->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    D3D12_RESOURCE_BARRIER afterCopy[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_reflTex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, sub),
        CD3DX12_RESOURCE_BARRIER::Transition(m_capTex.Get(),  D3D12_RESOURCE_STATE_COPY_DEST,   D3D12_RESOURCE_STATE_RENDER_TARGET,        sub) };
    cl->ResourceBarrier(2, afterCopy);

    // ---- 2. models over it ----
    cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, m_planarMirrorTable);
    if (m_shadowBuffer && m_shadowBufferMapped && m_shadowBufferStride > 0)
        cl->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_SHADOW_BUFFER,
            m_shadowBuffer->GetGPUVirtualAddress() + (static_cast<UINT64>(FrameCount) + m_frameIndex) * m_shadowBufferStride);

    CD3DX12_CPU_DESCRIPTOR_HANDLE capRtv(m_capRtvHeap.cpuStart, static_cast<INT>(sub), m_capRtvHeap.handleIncrementSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE capDsv(m_capDsvHeap.cpuStart);
    cl->OMSetRenderTargets(1, &capRtv, FALSE, &capDsv);
    cl->ClearDepthStencilView(capDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
    D3D12_RECT     sr = { 0, 0, static_cast<LONG>(size), static_cast<LONG>(size) };
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sr);

    float v[16], p[16];
    const float nearZ = std::max(static_cast<float>(config.myConfig.nearPlane), 0.05f);
    const float farZ  = std::max(static_cast<float>(config.myConfig.farPlane), nearZ + 1.0f);
    ReflectionCaptureFaceCamera(face, g_reflectionCapture.origin, nearZ, farZ, true, false, v, p);
    XMFLOAT4X4 v4, p4;
    std::memcpy(&v4, v, sizeof(v));
    std::memcpy(&p4, p, sizeof(p));
    const XMMATRIX capView = XMLoadFloat4x4(&v4);
    const XMMATRIX capProj = XMLoadFloat4x4(&p4);
    const XMFLOAT3 origin(g_reflectionCapture.origin[0], g_reflectionCapture.origin[1], g_reflectionCapture.origin[2]);

    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        Model& m = scene.scene_models[i];
        if (!m.m_isLoaded || m.m_modelInfo.bIsTransformProxy) continue;
        m.m_modelInfo.fxActive         = false;
        m.m_modelInfo.viewMatrix       = capView;
        m.m_modelInfo.projectionMatrix = capProj;
        m.m_modelInfo.cameraPosition   = origin;
        m.RenderDX12(cl, this, 0.0f, 1 + MAX_PLANAR_PLANES + face);
    }

    D3D12_RESOURCE_BARRIER toSRV = CD3DX12_RESOURCE_BARRIER::Transition(m_capTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, sub);
    cl->ResourceBarrier(1, &toSRV);

    // ---- 3. last face: rebuild mips 1.. with the downsample PSO ----
    if (lastFace)
    {
        if (mips > 1 && m_capDownPSO && m_capDownRootSig)
        {
            cl->SetGraphicsRootSignature(m_capDownRootSig.Get());
            cl->SetPipelineState(m_capDownPSO.Get());
            ID3D12DescriptorHeap* heaps[] = { m_cbvSrvUavHeap.heap.Get() };
            cl->SetDescriptorHeaps(1, heaps);
            cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            for (UINT m = 1; m < mips; ++m)
            {
                const UINT ms = static_cast<UINT>(ReflectionMipSize(m_capSize, static_cast<int>(m)));
                D3D12_VIEWPORT mvp = { 0.0f, 0.0f, static_cast<float>(ms), static_cast<float>(ms), 0.0f, 1.0f };
                D3D12_RECT     msr = { 0, 0, static_cast<LONG>(ms), static_cast<LONG>(ms) };
                cl->RSSetViewports(1, &mvp);
                cl->RSSetScissorRects(1, &msr);
                for (UINT f = 0; f < 6; ++f)
                {
                    const UINT dstSub = m + f * mips;
                    D3D12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(m_capTex.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, dstSub);
                    cl->ResourceBarrier(1, &toRT);
                    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_capRtvHeap.cpuStart, static_cast<INT>(f * mips + m), m_capRtvHeap.handleIncrementSize);
                    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                    CD3DX12_GPU_DESCRIPTOR_HANDLE srv(m_cbvSrvUavHeap.gpuStart,
                        static_cast<INT>(DX12_CAPTURE_SRV_BASE + f * (mips - 1) + (m - 1)), m_cbvSrvUavHeap.handleIncrementSize);
                    cl->SetGraphicsRootDescriptorTable(0, srv);
                    cl->DrawInstanced(3, 1, 0, 0);
                    D3D12_RESOURCE_BARRIER toPSR = CD3DX12_RESOURCE_BARRIER::Transition(m_capTex.Get(),
                        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dstSub);
                    cl->ResourceBarrier(1, &toPSR);
                }
            }
        }
        g_reflectionCapture.ready = true;
    }

    // The mip pass switched the root signature (all root arguments invalid) and the capture changed targets:
    // put the main pass back exactly as the model loop expects it.
    RestoreMainPassDX12();
}

// ===========================================================================================
// PlanarPlanDX12 — registers every reflector's plane for this frame and decides whether the planar
// array is active (see "Planar Reflections" in Lights.h).  Runs BEFORE RenderShadowPassDX12 so the
// b6 upload already carries the planes.
// ===========================================================================================
void DX12Renderer::PlanarPlanDX12()
{
    PlanarBeginPlan();

    if (config.myConfig.planarEnabled && m_shadowSRVTable.ptr != 0 && m_planarMirrorTable.ptr != 0 &&
        threadManager.threadVars.bLoaderTaskFinished.load())
    {
        const XMFLOAT3 cp = myCamera.GetPosition();
        const float camPos[3] = { cp.x, cp.y, cp.z };
        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            Model& m = scene.scene_models[i];
            m.m_modelInfo.planarPlaneIndex = -1;
            if (!m.m_isLoaded || m.bIsDestroyed || m.m_modelInfo.bIsTransformProxy || m.m_modelInfo.bIsTransformOnly) continue;
            if (!ModelIsPlanarReflector(m.m_modelInfo)) continue;
            XMFLOAT4X4 w;
            XMStoreFloat4x4(&w, m.m_modelInfo.worldMatrix);
            const float scale[3] = { m.m_modelInfo.scale.x, m.m_modelInfo.scale.y, m.m_modelInfo.scale.z };
            ModelPlanarRegister(m.m_modelInfo, &w._11, scale, camPos);
        }
    }

    if (g_planarFrame.planeCount > 0 && !m_planarTex && !m_planarFailed)
        CreatePlanarResourcesDX12();

    PlanarFrameBegin(m_planarTex != nullptr, static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight));
}

// ===========================================================================================
// RenderPlanarPassDX12 — planar reflection mirror renders (see "Planar Reflections" in Lights.h).
// Re-records every non-reflector model through each plane's mirrored camera into that plane's array
// slice using the SAME root signature + PSO as the main pass.  Per-model b0 data goes to constant-buffer
// slot 1 + plane (the main pass uses slot 0), so no pass overwrites another's data before the list executes.
// The root table points at the mirror copy whose t11 is NULL, so the planar array is never referenced by a
// bound table while it is a render target.
// Runs after RenderShadowPassDX12 (b6 + shadow maps bound) and after SetPipelineState, before the main loop.
// ===========================================================================================
void DX12Renderer::RenderPlanarPassDX12()
{
    ID3D12GraphicsCommandList* cl = m_commandList.Get();
    if (!cl || !g_planarFrame.active || !g_planarFrame.renderThisFrame || !m_planarTex) return;

    // Planar array -> render target; the root table switches to the mirror copy (NULL t11).
    D3D12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(m_planarTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &toRT);
    cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, m_planarMirrorTable);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(m_planarW), static_cast<float>(m_planarH), 0.0f, 1.0f };
    D3D12_RECT     sr = { 0, 0, static_cast<LONG>(m_planarW), static_cast<LONG>(m_planarH) };
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sr);

    // Real camera (row-vector float[16] == XMMATRIX bytes).
    XMFLOAT4X4 v4, p4;
    XMStoreFloat4x4(&v4, myCamera.GetViewMatrix());
    XMStoreFloat4x4(&p4, myCamera.GetProjectionMatrix());
    const XMFLOAT3 cp = myCamera.GetPosition();
    const float camPos[3] = { cp.x, cp.y, cp.z };

    CD3DX12_CPU_DESCRIPTOR_HANDLE planarDsv(m_planarDsvHeap.cpuStart);
    for (int plane = 0; plane < g_planarFrame.planeCount; ++plane)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE planarRtv(m_planarRtvHeap.cpuStart, plane, m_planarRtvHeap.handleIncrementSize);
        cl->OMSetRenderTargets(1, &planarRtv, FALSE, &planarDsv);
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        cl->ClearRenderTargetView(planarRtv, clearColor, 0, nullptr);
        cl->ClearDepthStencilView(planarDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        XMFLOAT4X4 mv4, mp4;
        float rcam[3];
        PlanarBuildCamera(&v4._11, &p4._11, camPos, g_planarFrame.planes[plane].n, g_planarFrame.planes[plane].d,
                          true, &mv4._11, &mp4._11, rcam);
        const XMMATRIX mirrorView = XMLoadFloat4x4(&mv4);
        const XMMATRIX mirrorProj = XMLoadFloat4x4(&mp4);

        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            Model& m = scene.scene_models[i];
            if (!m.m_isLoaded || m.m_modelInfo.bIsTransformProxy) continue;
            if (ModelIsPlanarReflector(m.m_modelInfo)) continue;                // a reflector never reflects itself

            m.m_modelInfo.fxActive         = false;
            m.m_modelInfo.viewMatrix       = mirrorView;
            m.m_modelInfo.projectionMatrix = mirrorProj;
            m.m_modelInfo.cameraPosition   = XMFLOAT3(rcam[0], rcam[1], rcam[2]);
            m.RenderDX12(cl, this, 0.0f, 1 + plane);                            // b0 slot 1 + plane
        }
    }

    // Back to the main pass: planar array readable again, main root table, main RT/DS + viewport.
    D3D12_RESOURCE_BARRIER toSRV = CD3DX12_RESOURCE_BARRIER::Transition(m_planarTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &toSRV);
    cl->SetGraphicsRootDescriptorTable(DX12_ROOT_PARAM_SHADOW_TABLE, CurrentShadowTableDX12());

    D3D12_VIEWPORT mainVP = { 0.0f, 0.0f, static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight), 0.0f, 1.0f };
    D3D12_RECT     mainSR = { 0, 0, static_cast<LONG>(iOrigWidth), static_cast<LONG>(iOrigHeight) };
    cl->RSSetViewports(1, &mainVP);
    cl->RSSetScissorRects(1, &mainSR);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(MainRTV(m_frameIndex));
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);
    cl->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    g_planarFrame.hasImage = true;
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

        // Planar reflection planning: registers reflector planes + decides active BEFORE b6 is uploaded.
        PlanarPlanDX12();

        // Scene reflection probe (t10): rebuild + copy on the open command list.  Must run BEFORE
        // RenderShadowPassDX12, which uploads b6 (reflection scale / mip range) and binds the t8-t10 table.
        UpdateReflectionProbeDX12(deltaTime);

        // Shadow depth passes (restores the main-pass state afterwards) + b6 / t8 / t9 / t10 binding.
        RenderShadowPassDX12();
        #if defined(_DEBUG)
            m_gpuProf.StampUpTo(m_commandList.Get(), DX12GpuProfiler::STAMP_SHADOWS);
        #endif

        // Set the DX12 PSO once for all model draws this frame
        if (m_pipelineState)
            m_commandList->SetPipelineState(m_pipelineState.Get());

        // Planar reflection mirror render (same PSO / root signature as the main pass), then the
        // main pass continues with the t11 table bound by RenderShadowPassDX12.
        RenderPlanarPassDX12();

        // Live scene capture into the reflection cube (one face per frame); restores the main pass state.
        RenderReflectionCaptureDX12(deltaTime);
        #if defined(_DEBUG)
            m_gpuProf.StampUpTo(m_commandList.Get(), DX12GpuProfiler::STAMP_REFLECTIONS);
        #endif

        // The planar / capture passes drew with the 1-sample PSO into 1-sample targets; the model loop below
        // draws into the main (possibly multisampled) target, so switch to the main-pass PSO.
        if (m_pipelineState)
            m_commandList->SetPipelineState(MainPSO());

        // Hoist camera reads once per frame — calling the getters inside the loop
        // would invoke them once per loaded model each frame.
        const auto viewMat = myCamera.GetViewMatrix();
        const auto projMat = myCamera.GetProjectionMatrix();
        const auto camPos  = myCamera.GetPosition();

        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            if (scene.scene_models[i].m_isLoaded && !scene.scene_models[i].m_modelInfo.bIsTransformProxy)
            {
                scene.scene_models[i].m_modelInfo.fxActive        = false;
                // MatrixCopy4x4F: 4 SSE MOVUPS loads+stores (64 bytes each) — avoids
                // XMMATRIX operator= overhead in the per-model hot loop.
                MatrixCopy4x4F(&viewMat, &scene.scene_models[i].m_modelInfo.viewMatrix);
                MatrixCopy4x4F(&projMat, &scene.scene_models[i].m_modelInfo.projectionMatrix);
                scene.scene_models[i].m_modelInfo.cameraPosition   = camPos;

                // Draw via the DX12 native command-list path.
                // RenderDX12() binds the upload-heap VB/IB/CB directly on the
                // command list, bypassing the D3D11-shader guard in Render().
                scene.scene_models[i].RenderDX12(m_commandList.Get(), this, deltaTime);
            }
        }

        #if defined(_DEBUG)
            m_gpuProf.StampUpTo(m_commandList.Get(), DX12GpuProfiler::STAMP_MODELS);
        #endif
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
        if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
            fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - halfH, halfW, halfH);
        else
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
    }

    // Spacebar skip — identical across all four renderers: the movie keeps playing
    // through the fade (no jarring freeze-frame cut) and is only stopped once
    // FadeOutThenCallback's own completion callback fires, so the fade duration
    // (1.0s) is the single source of truth instead of an immediate hard cut.
    // Only in SCENE_INTRO_MOVIE, not splash SCENE_INTRO.
    if (scene.stSceneType == SceneType::SCENE_INTRO_MOVIE && (GetAsyncKeyState(' ') & 0x8000) && !scene.bSceneSwitching)
    {
        scene.bSceneSwitching = true;
        fxManager.FadeOutThenCallback(XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f, 0.06f, []() {
            moviePlayer.Stop();
        });
    }
}

// ===========================================================================================
// RenderBackgroundImage — replaced by the STEP 3.5 D2D pre-pass in RenderFrame().
// The pre-pass blits the background, logo, starfield, and fireworks directly inside a
// properly paired AcquireWrappedResources/ReleaseWrappedResources block before the 3D
// command list executes, guaranteeing correct DX11On12 interop ordering.
// This function is retained as a no-op stub to satisfy the DX12Renderer.h declaration.
// ===========================================================================================
void DX12Renderer::RenderBackgroundImage()
{
    // No-op: all background rendering is handled by the STEP 3.5 pre-pass in RenderFrame().
    return;

    // DEAD CODE BELOW — kept in compiler-excluded block to document the former implementation.
    if (false)
    {
    m_d2dContext->BeginDraw();

    switch (scene.stSceneType)
    {
        case SceneType::SCENE_GAMETITLE:
        {
            if (threadManager.threadVars.bLoaderTaskFinished.load())
            {
                // Background image — render zoomed version at same position if FX is active
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)]) {
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                }

                // Company logo overlay at half size, bottom-left corner
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
                {
                    D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                    int halfW = static_cast<int>(logoSz.width  * 0.5f);
                    int halfH = static_cast<int>(logoSz.height * 0.5f);
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - halfH, halfW, halfH);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
                }

                // 3D starfield and warp-dot tunnel are rendered via fxManager once DX12
                // FXManager render paths are available (currently DX11-only).
            }
            else
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                {
                    // Consume (but no longer act on) the legacy black-fade-in trigger --
                    // the end-of-load pixel fader now owns the loading-screen reveal.
                    if (threadManager.threadVars.bInitiateFader.load())
                        threadManager.threadVars.bInitiateFader.store(false);
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                    else
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
                    // Consume (but no longer act on) the legacy black-fade-in trigger --
                    // the end-of-load pixel fader now owns the loading-screen reveal.
                    if (threadManager.threadVars.bInitiateFader.load())
                        threadManager.threadVars.bInitiateFader.store(false);
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                    else
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
            debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RenderBackgroundImage] EndDraw failed (0x%08X)", hr);
        }
    }
    catch (const std::exception& e)
    {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"[DX12 RenderBackgroundImage] EndDraw exception: %hs", e.what());
    }
    } // end if (false) — dead code block
} // End of RenderBackgroundImage()

#pragma warning(pop)

#endif // defined(__USE_DIRECTX_12__)
